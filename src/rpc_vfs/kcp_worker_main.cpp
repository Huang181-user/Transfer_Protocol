#include "rpc_vfs/vfs_packet.h"
#include "rpc_vfs/vfs_io_worker.h"
#include "common/logger.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <filesystem>
#include <thread>
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <mutex>
#include <chrono>

static bool send_all(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len > 0) { ssize_t n = send(fd, p, len, MSG_NOSIGNAL); if (n <= 0) return false; p += n; len -= n; }
    return true;
}

static bool recv_all(int fd, void* buf, size_t len) {
    char* p = (char*)buf;
    while (len > 0) { ssize_t n = recv(fd, p, len, MSG_WAITALL); if (n <= 0) return false; p += n; len -= n; }
    return true;
}

static void trim_path(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return ch != '\0' && !std::isspace(ch); }).base(), s.end());
}

static std::unordered_map<std::string, std::pair<uint32_t, uint64_t>> g_file_locks;
static std::mutex g_lock_mutex;

bool check_and_lock(const std::string& path, uint32_t client_id) {
    std::lock_guard<std::mutex> lock(g_lock_mutex);
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto it = g_file_locks.find(path);
    if (it != g_file_locks.end()) {
        if (it->second.first != client_id && (now - it->second.second < 15000)) return false; 
    }
    g_file_locks[path] = {client_id, now};
    return true;
}

void unlock_file(const std::string& path, uint32_t client_id) {
    std::lock_guard<std::mutex> lock(g_lock_mutex);
    auto it = g_file_locks.find(path);
    if (it != g_file_locks.end() && it->second.first == client_id) g_file_locks.erase(it);
}

void handle_client_connection(int client_fd) {
    ZHI_LOG_INFO("[KCP-WORKER] 🔗 UDS connection established. Awaiting I/O commands...");

    while (true) {
        uint32_t req_sz = 0;
        if (!recv_all(client_fd, &req_sz, 4)) break;

        std::vector<uint8_t> payload(req_sz);
        if (!recv_all(client_fd, payload.data(), req_sz)) break;
        if (payload.size() < sizeof(VfsPacketHeader)) break;

        VfsPacketHeader* header = reinterpret_cast<VfsPacketHeader*>(payload.data());
        if (payload.size() < sizeof(VfsPacketHeader) + header->path_len) break;

        std::string target_path(reinterpret_cast<char*>(payload.data() + sizeof(VfsPacketHeader)), header->path_len);
        trim_path(target_path);

        uint32_t client_id = (uint32_t)(header->session_id >> 32);

        std::vector<uint8_t> file_data;
        VfsOpcode resp_opcode = header->opcode;

        bool requires_lock = (header->opcode == VfsOpcode::OP_READ || header->opcode == VfsOpcode::OP_WRITE || header->opcode == VfsOpcode::OP_TRUNCATE);
                              
        if (requires_lock && !check_and_lock(target_path, client_id)) {
            ZHI_LOG_WARN("[FILE-LOCK] 🛑 File '" + target_path + "' is currently locked. Client [" + std::to_string(client_id) + "] access denied!");
            resp_opcode = VfsOpcode::OP_ERROR; 
        } else {
            auto start_time = std::chrono::high_resolution_clock::now();

            if (header->opcode == VfsOpcode::OP_STAT) {
                struct stat st;
                if (stat(target_path.c_str(), &st) == 0) {
                    uint64_t size = st.st_size;
                    uint8_t is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
                    uint64_t mtime = st.st_mtime;
                    uint64_t ctime = st.st_ctime;
                    uint64_t atime = st.st_atime;
                    uint32_t mode = st.st_mode;

                    // Dội bom đủ 37 bytes: [Size:8] [IsDir:1] [Mtime:8] [Ctime:8] [Atime:8] [Mode:4]
                    file_data.resize(37); 
                    std::memcpy(file_data.data(), &size, 8); 
                    file_data[8] = is_dir; 
                    std::memcpy(file_data.data() + 9, &mtime, 8); 
                    std::memcpy(file_data.data() + 17, &ctime, 8);
                    std::memcpy(file_data.data() + 25, &atime, 8);
                    std::memcpy(file_data.data() + 33, &mode, 4);
                } else { 
                    resp_opcode = VfsOpcode::OP_ERROR; 
                }
                ZHI_LOG_DEBUG("[OP_STAT] Path: " + target_path + " | Success: " + (resp_opcode == VfsOpcode::OP_ERROR ? "NO" : "YES"));

            } else if (header->opcode == VfsOpcode::OP_LIST) {
                std::string list_res;
                if (VfsIoWorker::list_directory(target_path, list_res)) { file_data.assign(list_res.begin(), list_res.end()); } else { resp_opcode = VfsOpcode::OP_ERROR; }
                ZHI_LOG_DEBUG("[OP_LIST] Path: " + target_path + " | Items length: " + std::to_string(list_res.length()));

            } else if (header->opcode == VfsOpcode::OP_READ) {
                ZHI_LOG_DEBUG("[OP_READ] REQUEST: Path=" + target_path + " | Offset=" + std::to_string(header->offset) + " | ReqSize=" + std::to_string(header->data_len));
                if (!VfsIoWorker::read_block(target_path, header->offset, header->data_len, file_data)) {
                     resp_opcode = VfsOpcode::OP_ERROR;
                     ZHI_LOG_ERR("[OP_READ] FAILED to read data from filesystem.");
                } else {
                     ZHI_LOG_DEBUG("[OP_READ] SUCCESS: Read " + std::to_string(file_data.size()) + " bytes.");
                }

            } else if (header->opcode == VfsOpcode::OP_WRITE) {
                size_t data_offset = sizeof(VfsPacketHeader) + header->path_len;
                std::vector<uint8_t> write_payload;
                if (payload.size() > data_offset) write_payload.assign(payload.begin() + data_offset, payload.end());
                ZHI_LOG_DEBUG("[OP_WRITE] Path: " + target_path + " | Offset: " + std::to_string(header->offset) + " | Size: " + std::to_string(write_payload.size()));
                if (!std::filesystem::exists(target_path)) { std::ofstream touch_file(target_path, std::ios::app | std::ios::binary); touch_file.close(); }
                if (!write_payload.empty()) { if (!VfsIoWorker::write_block(target_path, header->offset, write_payload)) resp_opcode = VfsOpcode::OP_ERROR; }

            } else if (header->opcode == VfsOpcode::OP_RENAME) {
                size_t data_offset = sizeof(VfsPacketHeader) + header->path_len;
                if (payload.size() >= data_offset + header->data_len) {
                    std::string new_path(reinterpret_cast<char*>(payload.data() + data_offset), header->data_len);
                    trim_path(new_path); 
                    if (!check_and_lock(target_path, client_id) || !check_and_lock(new_path, client_id)) {
                        resp_opcode = VfsOpcode::OP_ERROR;
                    } else {
                        try { if (std::filesystem::exists(new_path)) std::filesystem::remove_all(new_path); std::filesystem::rename(target_path, new_path); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }
                        unlock_file(target_path, client_id); unlock_file(new_path, client_id);
                    }
                    ZHI_LOG_INFO("[OP_RENAME] " + target_path + " -> " + new_path);
                } else { resp_opcode = VfsOpcode::OP_ERROR; }

            } else if (header->opcode == VfsOpcode::OP_TRUNCATE) {
                ZHI_LOG_DEBUG("[OP_TRUNCATE] Path: " + target_path + " | New Size: " + std::to_string(header->offset));
                try { if (!std::filesystem::exists(target_path)) { std::ofstream touch_file(target_path, std::ios::out | std::ios::binary); touch_file.close(); } std::filesystem::resize_file(target_path, header->offset); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }

            } else if (header->opcode == VfsOpcode::OP_MKDIR) {
                ZHI_LOG_INFO("[OP_MKDIR] Path: " + target_path);
                try { std::filesystem::create_directories(target_path); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }

            } else if (header->opcode == VfsOpcode::OP_DELETE) {
                ZHI_LOG_INFO("[OP_DELETE] Path: " + target_path);
                if (!check_and_lock(target_path, client_id)) { resp_opcode = VfsOpcode::OP_ERROR; } 
                else { try { std::filesystem::remove_all(target_path); unlock_file(target_path, client_id); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; } }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            ZHI_LOG_DEBUG("[IO-PERF] Opcode 0x" + std::to_string((int)header->opcode) + " completed in " + std::to_string(duration.count()) + " us");
        }

        VfsPacketHeader res_hdr = *header;
        res_hdr.opcode = resp_opcode;
        res_hdr.data_len = file_data.size();

        uint32_t resp_sz = sizeof(res_hdr) + file_data.size();
        if (!send_all(client_fd, &resp_sz, 4)) break;
        if (!send_all(client_fd, &res_hdr, sizeof(res_hdr))) break;
        if (!file_data.empty()) { if (!send_all(client_fd, file_data.data(), file_data.size())) break; }
    }
    close(client_fd);
    ZHI_LOG_INFO("[KCP-WORKER] UDS connection closed.");
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::string uds_path = argv[1];
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX; strncpy(addr.sun_path, uds_path.c_str(), sizeof(addr.sun_path)-1);
    unlink(uds_path.c_str());
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    chmod(uds_path.c_str(), 0777); 
    listen(server_fd, SOMAXCONN);
    while (true) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        std::thread(handle_client_connection, client_fd).detach();
    }
    close(server_fd); unlink(uds_path.c_str());
    return 0;
}

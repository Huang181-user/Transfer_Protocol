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
#include <algorithm> // 🔥 KHÁNG HUYẾT THANH TRỊ LỖI std::find_if
#include <cctype>    // 🔥 KHÁNG HUYẾT THANH TRỊ LỖI std::isspace

static bool send_all(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static bool recv_all(int fd, void* buf, size_t len) {
    char* p = (char*)buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, MSG_WAITALL);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

// Hàm Trim gọt sạch rác \0 và khoảng trắng ở đuôi chuỗi
static void trim_path(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return ch != '\0' && !std::isspace(ch);
    }).base(), s.end());
}

void handle_client_connection(int client_fd) {
    ZHI_LOG_INFO("[KCP-WORKER] 🔗 Đã thiết lập đường ống UDS. Chờ lệnh I/O...");

    while (true) {
        uint32_t req_sz = 0;
        if (!recv_all(client_fd, &req_sz, 4)) break;

        std::vector<uint8_t> payload(req_sz);
        if (!recv_all(client_fd, payload.data(), req_sz)) break;
        if (payload.size() < sizeof(VfsPacketHeader)) break;

        VfsPacketHeader* header = reinterpret_cast<VfsPacketHeader*>(payload.data());
        if (payload.size() < sizeof(VfsPacketHeader) + header->path_len) break;

        std::string target_path(reinterpret_cast<char*>(payload.data() + sizeof(VfsPacketHeader)), header->path_len);
        
        // 🔥 GỌT SẠCH RÁC NULL TERMINATOR! TRÁNH LỖI OS ERROR 2
        trim_path(target_path);

        std::vector<uint8_t> file_data;
        VfsOpcode resp_opcode = header->opcode;

        if (header->opcode == VfsOpcode::OP_STAT) {
            uint64_t size = 0; bool is_dir = false;
            if (VfsIoWorker::stat_file(target_path, size, is_dir)) {
                uint64_t mtime = 0;
                struct stat st;
                if (stat(target_path.c_str(), &st) == 0) mtime = st.st_mtime;

                file_data.resize(17); 
                std::memcpy(file_data.data(), &size, 8); 
                file_data[8] = is_dir ? 1 : 0;
                std::memcpy(file_data.data() + 9, &mtime, 8); 
            } else { resp_opcode = VfsOpcode::OP_ERROR; }
        } else if (header->opcode == VfsOpcode::OP_LIST) {
            std::string list_res;
            if (VfsIoWorker::list_directory(target_path, list_res)) { file_data.assign(list_res.begin(), list_res.end()); } else { resp_opcode = VfsOpcode::OP_ERROR; }
        } else if (header->opcode == VfsOpcode::OP_READ) {
            if (!VfsIoWorker::read_block(target_path, header->offset, header->data_len, file_data)) resp_opcode = VfsOpcode::OP_ERROR;
        } else if (header->opcode == VfsOpcode::OP_WRITE) {
            size_t data_offset = sizeof(VfsPacketHeader) + header->path_len;
            std::vector<uint8_t> write_payload;
            if (payload.size() > data_offset) write_payload.assign(payload.begin() + data_offset, payload.end());

            if (!std::filesystem::exists(target_path)) {
                std::ofstream touch_file(target_path, std::ios::app | std::ios::binary);
                touch_file.close();
            }
            if (!write_payload.empty()) {
                if (!VfsIoWorker::write_block(target_path, header->offset, write_payload)) resp_opcode = VfsOpcode::OP_ERROR;
            }
        } else if (header->opcode == VfsOpcode::OP_RENAME) {
            size_t data_offset = sizeof(VfsPacketHeader) + header->path_len;
            if (payload.size() >= data_offset + header->data_len) {
                std::string new_path(reinterpret_cast<char*>(payload.data() + data_offset), header->data_len);
                trim_path(new_path); // Gọt cả new path cho chắc!
                try {
                    if (std::filesystem::exists(new_path)) std::filesystem::remove_all(new_path);
                    std::filesystem::rename(target_path, new_path);
                } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }
            } else { resp_opcode = VfsOpcode::OP_ERROR; }
        } else if (header->opcode == VfsOpcode::OP_TRUNCATE) {
            try {
                if (!std::filesystem::exists(target_path)) {
                    std::ofstream touch_file(target_path, std::ios::out | std::ios::binary);
                    touch_file.close();
                }
                std::filesystem::resize_file(target_path, header->offset);
            } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }
        } else if (header->opcode == VfsOpcode::OP_MKDIR) {
            try { std::filesystem::create_directories(target_path); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }
        } else if (header->opcode == VfsOpcode::OP_DELETE) {
            try { std::filesystem::remove_all(target_path); } catch (...) { resp_opcode = VfsOpcode::OP_ERROR; }
        }

        VfsPacketHeader res_hdr = *header;
        res_hdr.opcode = resp_opcode;
        res_hdr.data_len = file_data.size();

        uint32_t resp_sz = sizeof(res_hdr) + file_data.size();
        if (!send_all(client_fd, &resp_sz, 4)) break;
        if (!send_all(client_fd, &res_hdr, sizeof(res_hdr))) break;
        if (!file_data.empty()) {
            if (!send_all(client_fd, file_data.data(), file_data.size())) break;
        }
    }
    close(client_fd);
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

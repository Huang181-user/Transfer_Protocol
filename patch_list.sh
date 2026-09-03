cat << 'IN_EOF' > src/rpc_vfs/vfs_io_worker.cpp
#include "rpc_vfs/vfs_io_worker.h"
#include "common/logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
#include <cstring>
#include <unordered_map>
#include <mutex>

namespace fs = std::filesystem;

static std::unordered_map<std::string, int> g_fd_cache;
static std::mutex g_fd_mutex;

static int get_or_open_fd(const std::string& path, int flags, mode_t mode = 0) {
    std::lock_guard<std::mutex> lock(g_fd_mutex);
    auto it = g_fd_cache.find(path);
    if (it != g_fd_cache.end()) return it->second;
    int fd = open(path.c_str(), flags, mode);
    if (fd >= 0) {
        if (g_fd_cache.size() > 50) {
            auto first_it = g_fd_cache.begin();
            close(first_it->second);
            g_fd_cache.erase(first_it);
        }
        g_fd_cache[path] = fd;
    }
    return fd;
}

bool VfsIoWorker::read_block(const std::string& path, uint64_t offset, uint32_t length, std::vector<uint8_t>& out_buffer) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) { ZHI_LOG_ERR("[IO-READ] Thất bại khi open file: " + path); return false; }
    out_buffer.resize(length);
    ssize_t bytes_read = pread(fd, out_buffer.data(), length, offset);
    close(fd);
    if (bytes_read < 0) return false;
    out_buffer.resize(bytes_read);
    return true;
}

bool VfsIoWorker::write_block(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data) {
    int fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (fd < 0) { ZHI_LOG_ERR("[IO-WRITE] Thất bại khi open file: " + path); return false; }
    if (!data.empty()) {
        ssize_t bytes_written = pwrite(fd, data.data(), data.size(), offset);
        close(fd);
        return bytes_written == static_cast<ssize_t>(data.size());
    }
    close(fd);
    return true;
}

bool VfsIoWorker::stat_file(const std::string& path, uint64_t& out_size, bool& out_is_dir) {
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0) return false;
    out_size = file_stat.st_size;
    out_is_dir = S_ISDIR(file_stat.st_mode);
    return true;
}

bool VfsIoWorker::list_directory(const std::string& path, std::string& out_list) {
    try {
        out_list.clear();
        for (const auto& entry : fs::directory_iterator(path)) {
            std::error_code ec;
            uint64_t size = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
            uint8_t is_dir = entry.is_directory(ec) ? 1 : 0;
            
            // 🔥 ĐỒNG BỘ CHUẨN 39 BYTES VỚI ANDROID KOTLIN (Lấy thêm time)
            struct stat st;
            uint64_t mtime = 0, ctime = 0, atime = 0;
            uint32_t mode = 33188; 
            
            if (stat(entry.path().string().c_str(), &st) == 0) {
                mtime = st.st_mtime;
                ctime = st.st_ctime;
                atime = st.st_atime;
                mode = st.st_mode;
            }
            
            std::string name = entry.path().filename().string();
            uint16_t name_len = static_cast<uint16_t>(name.length());

            char buf[39];
            buf[0] = name_len & 0xFF; buf[1] = (name_len >> 8) & 0xFF;
            buf[2] = is_dir;
            std::memcpy(buf + 3, &size, 8);
            std::memcpy(buf + 11, &mtime, 8);
            std::memcpy(buf + 19, &ctime, 8);
            std::memcpy(buf + 27, &atime, 8);
            std::memcpy(buf + 35, &mode, 4);

            out_list.append(buf, 39);
            out_list.append(name);
        }
        return true;
    } catch (...) { return false; }
}

void VfsIoWorker::close_cached_fd(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_fd_mutex);
    auto it = g_fd_cache.find(path);
    if (it != g_fd_cache.end()) {
        close(it->second);
        g_fd_cache.erase(it);
    }
}
IN_EOF

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

// BỘ ĐỆM CACHE FILE DESCRIPTOR TỰ ĐỘNG
static std::unordered_map<std::string, int> g_fd_cache;
static std::mutex g_fd_mutex;

static int get_or_open_fd(const std::string& path, int flags, mode_t mode = 0) {
    std::lock_guard<std::mutex> lock(g_fd_mutex);
    auto it = g_fd_cache.find(path);
    if (it != g_fd_cache.end()) {
        return it->second;
    }
    int fd = open(path.c_str(), flags, mode);
    if (fd >= 0) {
        // Giới hạn cache tối đa 50 file mở cùng lúc
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
    int fd = get_or_open_fd(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    out_buffer.resize(length);
    ssize_t bytes_read = pread(fd, out_buffer.data(), length, offset);
    if (bytes_read < 0) return false;
    out_buffer.resize(bytes_read);
    return true;
}

bool VfsIoWorker::write_block(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data) {
    int fd = get_or_open_fd(path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    if (!data.empty()) {
        ssize_t bytes_written = pwrite(fd, data.data(), data.size(), offset);
        return bytes_written == static_cast<ssize_t>(data.size());
    }
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
        std::string list_res = "";
        for (const auto& entry : fs::directory_iterator(path)) {
            list_res += entry.path().filename().string() + "," + (entry.is_directory() ? "DIR" : "REG") + "|";
        }
        out_list = list_res;
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

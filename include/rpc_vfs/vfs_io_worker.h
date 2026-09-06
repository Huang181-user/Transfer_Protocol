#ifndef ZHIAUTH_VFS_IO_WORKER_H
#define ZHIAUTH_VFS_IO_WORKER_H
#include <string>
#include <vector>
#include <cstdint>

class VfsIoWorker {
public:
    static bool read_block(const std::string& path, uint64_t offset, uint32_t length, std::vector<uint8_t>& out_buffer);
    static bool write_block(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data);
    static bool stat_file(const std::string& path, uint64_t& out_size, bool& out_is_dir);
    static bool list_directory(const std::string& path, std::string& out_list);
    static void close_cached_fd(const std::string& path);
};
#endif

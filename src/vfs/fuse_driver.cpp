#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include "vfs/fuse_driver.h"
#include "rpc_client/vfs_packet.h"
#include "rpc_client/vfs_client.h"
#include "rpc_quic/msquic_client.h"
#include "common/logger.h"
#include <cstring>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <sys/stat.h>

extern VfsClient* g_vfs_client;
std::string g_remote_base;
std::atomic<uint32_t> g_req_id{1};

struct FileMeta { bool is_dir; uint64_t size; uint32_t mode; time_t exp; uint64_t mtime; uint64_t ctime; uint64_t atime; };
static std::unordered_map<std::string, FileMeta> g_ram_cache;
static std::mutex g_cache_mtx;

void InjectBulkCache(const std::string& parentPath, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(g_cache_mtx);
    if (payload.size() <= sizeof(VfsPacketHeader)) return;
    const uint8_t* p = payload.data() + sizeof(VfsPacketHeader);
    size_t total = payload.size() - sizeof(VfsPacketHeader);
    size_t cur = 0;
    while (cur + 39 <= total) {
        uint16_t nameLen; memcpy(&nameLen, p + cur, 2);
        uint8_t isDir = p[cur + 2];
        uint64_t size; memcpy(&size, p + cur + 3, 8);
        uint64_t mtime; memcpy(&mtime, p + cur + 11, 8);
        uint64_t ctime; memcpy(&ctime, p + cur + 19, 8);
        uint64_t atime; memcpy(&atime, p + cur + 27, 8);
        uint32_t mode; memcpy(&mode, p + cur + 35, 4);
        cur += 39;
        if (cur + nameLen > total) break;
        std::string name((char*)(p + cur), nameLen);
        cur += nameLen;
        std::string fullPath = parentPath + "/" + name;
        if (parentPath == "/") fullPath = "/" + name;
        g_ram_cache[fullPath] = {isDir == 1, size, mode, time(NULL) + 60, mtime, ctime, atime};
    }
}

bool CheckRAMCache(const std::string& fullPath, FileMeta& out_meta) {
    std::lock_guard<std::mutex> lock(g_cache_mtx);
    auto it = g_ram_cache.find(fullPath);
    if (it != g_ram_cache.end() && time(NULL) < it->second.exp) {
        out_meta = it->second; return true;
    }
    return false;
}

void RemoveFromRAMCache(const std::string& fullPath) {
    std::lock_guard<std::mutex> lock(g_cache_mtx); g_ram_cache.erase(fullPath);
}

std::vector<uint8_t> DispatchRpc(VfsOpcode opcode, const std::string& path, uint64_t offset, uint32_t reqLen, const std::vector<uint8_t>& in_data, int override_kcp = -1, bool is_async = false) {
    uint32_t req_id = g_req_id.fetch_add(1);
    uint32_t data_len = (opcode == VfsOpcode::OP_READ) ? reqLen : in_data.size();
    std::vector<uint8_t> req(sizeof(VfsPacketHeader) + path.size() + in_data.size());
    
    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req.data());
    hdr->magic = 0x5A484941; hdr->opcode = opcode; 
    if (g_vfs_client) {
        hdr->session_id = ((uint64_t)g_vfs_client->get_client_id() << 32) | req_id;
    } else {
        hdr->session_id = req_id;
    }
    hdr->offset = offset; hdr->data_len = data_len; hdr->path_len = path.size();

    memcpy(req.data() + sizeof(VfsPacketHeader), path.data(), path.size());
    if (!in_data.empty()) memcpy(req.data() + sizeof(VfsPacketHeader) + path.size(), in_data.data(), in_data.size());

    bool use_kcp = true;
    if (override_kcp != -1) {
        use_kcp = (override_kcp == 1);
    } else if (fuse_get_context() != nullptr && fuse_get_context()->private_data != nullptr) {
        use_kcp = (bool)fuse_get_context()->private_data;
    }
    
    if (use_kcp && g_vfs_client) {
        if (is_async) {
            g_vfs_client->send_rpc_async(req, req_id);
            return {1}; // Fake response thành công
        } else {
            auto res = g_vfs_client->send_rpc_sync(req, req_id);
            if (!res.empty() && res.size() >= sizeof(VfsPacketHeader)) {
                VfsPacketHeader* resHdr = reinterpret_cast<VfsPacketHeader*>(res.data());
                if (resHdr->opcode != VfsOpcode::OP_ERROR) return res;
            }
        }
    }
    
    if (is_async) {
        MsQuicClient::send_vfs_async(req, req_id);
        return {1};
    } else {
        auto fallback_res = MsQuicClient::send_vfs_sync(req, req_id);
        if (!fallback_res.empty()) return fallback_res;
        return {};
    }
}

struct WriteBuffer {
    std::mutex mu;
    std::string realPath;
    uint64_t startOffset;
    std::vector<uint8_t> data;
    bool isFlushing;
    bool use_kcp;
};

// static std::unordered_map<std::string, std::shared_ptr<WriteBuffer>> g_write_buffers;
// static std::mutex g_wb_mtx;

// void FlushChunkAsync(std::shared_ptr<WriteBuffer> wb, bool forceAll) {
//     while (true) {
//         wb->mu.lock();
//         if (wb->data.empty() || (!forceAll && wb->data.size() < 2097152)) {
//             wb->isFlushing = false;
//             wb->mu.unlock();
//             return;
//         }

//         size_t toSendLen = wb->data.size();
//         if (toSendLen > 2097152) toSendLen = 2097152;

//         std::vector<uint8_t> chunk(wb->data.begin(), wb->data.begin() + toSendLen);
//         uint64_t currentOffset = wb->startOffset;

//         wb->startOffset += toSendLen;
//         wb->data.erase(wb->data.begin(), wb->data.begin() + toSendLen);
//         wb->mu.unlock();

//         DispatchRpc(VfsOpcode::OP_WRITE, wb->realPath, currentOffset, 0, chunk, wb->use_kcp ? 1 : 0);
//     }
// }

static int vfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    memset(stbuf, 0, sizeof(struct stat));
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;

    if (std::string(path) == "/" || full_path == g_remote_base) {
        stbuf->st_mode = S_IFDIR | 0777; stbuf->st_nlink = 2; stbuf->st_uid = getuid(); stbuf->st_gid = getgid(); 
        stbuf->st_mtime = time(NULL); stbuf->st_atime = time(NULL); stbuf->st_ctime = time(NULL);
        return 0;
    }

    FileMeta meta;
    if (CheckRAMCache(full_path, meta)) {
        stbuf->st_mode = meta.is_dir ? (S_IFDIR | 0777) : meta.mode;
        stbuf->st_nlink = meta.is_dir ? 2 : 1; stbuf->st_size = meta.size;
        stbuf->st_uid = getuid(); stbuf->st_gid = getgid();
        stbuf->st_mtime = meta.mtime; stbuf->st_atime = meta.atime; stbuf->st_ctime = meta.ctime;
        return 0;
    }

    auto res = DispatchRpc(VfsOpcode::OP_STAT, full_path, 0, 0, {});
    if (res.size() < sizeof(VfsPacketHeader) + 37) return -ENOENT;

    uint8_t* payload = res.data() + sizeof(VfsPacketHeader);
    uint64_t size; memcpy(&size, payload, 8); uint8_t isDir = payload[8];
    uint64_t mtime, ctime, atime;
    memcpy(&mtime, payload + 9, 8); memcpy(&ctime, payload + 17, 8); memcpy(&atime, payload + 25, 8);
    uint32_t modeRaw; memcpy(&modeRaw, payload + 33, 4);

    stbuf->st_mode = isDir ? (S_IFDIR | 0777) : modeRaw;
    stbuf->st_nlink = isDir ? 2 : 1; stbuf->st_size = size;
    stbuf->st_uid = getuid(); stbuf->st_gid = getgid();
    stbuf->st_mtime = mtime; stbuf->st_ctime = ctime; stbuf->st_atime = atime;
    
    std::lock_guard<std::mutex> lock(g_cache_mtx);
    g_ram_cache[full_path] = {isDir == 1, size, modeRaw, time(NULL) + 60, mtime, ctime, atime};
    
    return 0;
}

static ino_t string_to_ino(const std::string& path) {
    ino_t hash = 5381; for (char c : path) hash = ((hash << 5) + hash) + c; return hash;
}

static int vfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0)); filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));

    auto res = DispatchRpc(VfsOpcode::OP_LIST, full_path, 0, 0, {});
    if (res.size() <= sizeof(VfsPacketHeader)) return 0;
    InjectBulkCache(full_path, res);

    uint8_t* p = res.data() + sizeof(VfsPacketHeader);
    size_t total = res.size() - sizeof(VfsPacketHeader); size_t cur = 0;

    while (cur + 39 <= total) {
        uint16_t nameLen; memcpy(&nameLen, p + cur, 2); uint8_t isDir = p[cur + 2];
        uint64_t size; memcpy(&size, p + cur + 3, 8);
        uint64_t mtime; memcpy(&mtime, p + cur + 11, 8);
        uint64_t ctime; memcpy(&ctime, p + cur + 19, 8);
        uint64_t atime; memcpy(&atime, p + cur + 27, 8);
        uint32_t mode; memcpy(&mode, p + cur + 35, 4);
        cur += 39; if (cur + nameLen > total) break;
        std::string name((char*)(p + cur), nameLen); cur += nameLen;
        struct stat st; memset(&st, 0, sizeof(st));
        st.st_ino = string_to_ino(full_path + "/" + name);
        st.st_mode = isDir ? (S_IFDIR | 0777) : mode;
        st.st_size = size; st.st_mtime = mtime; st.st_ctime = ctime; st.st_atime = atime; st.st_mtime = mtime; st.st_ctime = ctime; st.st_atime = atime;
        filler(buf, name.c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0));
    }
    return 0;
}

static int vfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    auto res = DispatchRpc(VfsOpcode::OP_READ, full_path, offset, size, {});
    if (res.size() <= sizeof(VfsPacketHeader)) return -EIO;
    size_t data_sz = res.size() - sizeof(VfsPacketHeader); memcpy(buf, res.data() + sizeof(VfsPacketHeader), data_sz);
    return data_sz;
}

static int vfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    
    RemoveFromRAMCache(full_path);

    if (size == 0) {
        DispatchRpc(VfsOpcode::OP_WRITE, full_path, offset, 0, {}, -1, false);
        return 0;
    }
    
    std::vector<uint8_t> data(buf, buf + size);
    // 🔥 GỌI ASYNC GHI FILE SIÊU TỐC, KHÔNG CẦN CHỜ!
    auto res = DispatchRpc(VfsOpcode::OP_WRITE, full_path, offset, 0, data, -1, true);
    return res.empty() ? -EIO : size;
}

static int vfs_mkdir(const char* path, mode_t mode) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    auto res = DispatchRpc(VfsOpcode::OP_MKDIR, full_path, 0, 0, {});
    return res.empty() ? -EIO : 0;
}

static int vfs_unlink(const char* path) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    RemoveFromRAMCache(full_path);
    auto res = DispatchRpc(VfsOpcode::OP_DELETE, full_path, 0, 0, {});
    return res.empty() ? -EIO : 0;
}

static int vfs_rmdir(const char* path) { return vfs_unlink(path); }
static int vfs_rename(const char* oldpath, const char* newpath, unsigned int flags) {
    std::string full_old = g_remote_base; if (std::string(oldpath) != "/") full_old += oldpath;
    std::string full_new = g_remote_base; if (std::string(newpath) != "/") full_new += newpath;
    RemoveFromRAMCache(full_old); RemoveFromRAMCache(full_new);
    std::vector<uint8_t> new_p(full_new.begin(), full_new.end());
    auto res = DispatchRpc(VfsOpcode::OP_RENAME, full_old, 0, 0, new_p);
    return res.empty() ? -EIO : 0;
}

static int vfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    auto res = DispatchRpc(VfsOpcode::OP_WRITE, full_path, 0, 0, {}); 
    return res.empty() ? -EIO : 0;
}

static int vfs_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
    std::string full_path = g_remote_base;
    if (std::string(path) != "/") full_path += path;
    RemoveFromRAMCache(full_path);
    auto res = DispatchRpc(VfsOpcode::OP_TRUNCATE, full_path, size, 0, {}); return res.empty() ? -EIO : 0;
}

static int vfs_open(const char* path, struct fuse_file_info* fi) { return 0; }
static int vfs_fsync(const char* path, int isdatasync, struct fuse_file_info* fi) { return 0; }

static int vfs_flush(const char* path, struct fuse_file_info* fi) {
    // 🔥 Đã xóa bộ đệm rườm rà, trả về 0 luôn cho nhẹ nợ
    return 0;
}

static struct fuse_operations vfs_oper = {
    .getattr = vfs_getattr, .mkdir = vfs_mkdir, .unlink = vfs_unlink, .rmdir = vfs_rmdir, .rename = vfs_rename, .truncate = vfs_truncate,
    .open = vfs_open, .read = vfs_read, .write = vfs_write, .flush = vfs_flush, .fsync = vfs_fsync, .readdir = vfs_readdir, .create = vfs_create,
};

int FuseDriver::start_fuse(const std::string& mountpoint, const std::string& remote_base, bool use_kcp) {
    g_remote_base = remote_base;
    // Bỏ max_read và max_write đi vì libfuse3 trên Linux không hỗ trợ nhồi vào argv
    char* argv[] = { 
        (char*)"zhiauth_fuse", 
        (char*)mountpoint.c_str(), 
        (char*)"-f", 
        (char*)"-o", 
        (char*)"allow_other,kernel_cache,auto_cache" 
    };
    ZHI_LOG_INFO("[FUSE-DRIVER] Kích hoạt Ổ đĩa ảo C++ tại: " + mountpoint);
    return fuse_main(5, argv, &vfs_oper, (void*)use_kcp);
}

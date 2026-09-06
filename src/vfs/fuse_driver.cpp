#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <sys/stat.h>
#include "vfs/fuse_driver.h"
#include "rpc_client/vfs_packet.h"
#include "rpc_client/vfs_client.h"
#include "rpc_quic/msquic_client.h"
#include "common/logger.h"
#include <cstring>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <stdexcept>

extern VfsClient* g_vfs_client;
static std::string g_remote_base;
static std::atomic<uint32_t> g_req_id{1};

// BỘ ĐỆM RAM CACHE SIÊU TỐC
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
        g_ram_cache[fullPath] = {isDir == 1, size, mode, time(NULL) + 120, mtime, ctime, atime};
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

// Thêm cờ is_async vào DispatchRpc
std::vector<uint8_t> DispatchRpc(VfsOpcode opcode, const std::string& path, uint64_t offset, uint32_t reqLen, const std::vector<uint8_t>& in_data, bool is_async = false) {
    try {
        uint32_t req_id = g_req_id.fetch_add(1);
        uint32_t data_len = (opcode == VfsOpcode::OP_READ) ? reqLen : in_data.size();
        std::vector<uint8_t> req(sizeof(VfsPacketHeader) + path.size() + in_data.size());
        
        VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req.data());
        hdr->magic = 0x5A484941; 
        hdr->opcode = opcode; 
        
        if (g_vfs_client) {
            hdr->session_id = ((uint64_t)g_vfs_client->get_client_id() << 32) | req_id;
        } else {
            hdr->session_id = req_id;
        }
        
        hdr->offset = offset; 
        hdr->data_len = data_len; 
        hdr->path_len = static_cast<uint16_t>(path.size());

        memcpy(req.data() + sizeof(VfsPacketHeader), path.data(), path.size());
        if (!in_data.empty()) memcpy(req.data() + sizeof(VfsPacketHeader) + path.size(), in_data.data(), in_data.size());

        bool use_kcp = true;
        if (fuse_get_context() != nullptr && fuse_get_context()->private_data != nullptr) {
            use_kcp = (bool)fuse_get_context()->private_data;
        }

        if (use_kcp && g_vfs_client) {
            // 🔥 NẾU LÀ GHI FILE -> BẮN ASYNC RỒI RÚT LUI NGAY MÀ KHÔNG CHỜ!
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
        
        auto fallback_res = MsQuicClient::send_vfs_sync(req, req_id);
        if (!fallback_res.empty()) return fallback_res;
        return {};
    } catch (const std::exception& e) {
        ZHI_LOG_ERR("CRASH in DispatchRpc: " + std::string(e.what()));
        return {};
    } catch (...) {
        ZHI_LOG_ERR("FATAL UNKNOWN CRASH in DispatchRpc!");
        return {};
    }
}

static int vfs_getattr(const char* path, struct fuse_stat* stbuf) {
    try {
        memset(stbuf, 0, sizeof(struct fuse_stat));
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;

        if (std::string(path) == "/" || full_path == g_remote_base) {
            stbuf->st_mode = S_IFDIR | 0777; stbuf->st_nlink = 2;
            return 0;
        }

        FileMeta meta;
        if (CheckRAMCache(full_path, meta)) {
            stbuf->st_mode = meta.is_dir ? (S_IFDIR | 0777) : (S_IFREG | 0777);
            stbuf->st_nlink = meta.is_dir ? 2 : 1;
            stbuf->st_size = meta.size;
            stbuf->st_mtim.tv_sec = meta.mtime; stbuf->st_ctim.tv_sec = meta.ctime; stbuf->st_atim.tv_sec = meta.atime;
            return 0;
        }

        auto res = DispatchRpc(VfsOpcode::OP_STAT, full_path, 0, 0, {});
        if (res.size() < sizeof(VfsPacketHeader) + 37) return -ENOENT;

        uint8_t* payload = res.data() + sizeof(VfsPacketHeader);
        uint64_t size; memcpy(&size, payload, 8);
        uint8_t isDir = payload[8];
        uint64_t mtime, ctime, atime;
        memcpy(&mtime, payload + 9, 8);
        memcpy(&ctime, payload + 17, 8);
        memcpy(&atime, payload + 25, 8);

        stbuf->st_mode = isDir ? (S_IFDIR | 0777) : (S_IFREG | 0777);
        stbuf->st_nlink = isDir ? 2 : 1;
        stbuf->st_size = size;
        stbuf->st_mtim.tv_sec = mtime; stbuf->st_ctim.tv_sec = ctime; stbuf->st_atim.tv_sec = atime;
        
        std::lock_guard<std::mutex> lock(g_cache_mtx);
        g_ram_cache[full_path] = {isDir == 1, size, 33188, time(NULL) + 120, mtime, ctime, atime};

        return 0;
    } catch (...) { return -EIO; }
}

static int vfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, fuse_off_t offset, struct fuse_file_info* fi) {
    try {
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;

        if (filler(buf, ".", NULL, 0)) return 0;
        if (filler(buf, "..", NULL, 0)) return 0;

        auto res = DispatchRpc(VfsOpcode::OP_LIST, full_path, 0, 0, {});
        if (res.size() <= sizeof(VfsPacketHeader)) return 0;

        InjectBulkCache(full_path, res);

        uint8_t* p = res.data() + sizeof(VfsPacketHeader);
        size_t total = res.size() - sizeof(VfsPacketHeader); size_t cur = 0;

        while (cur + 39 <= total) {
            uint16_t nameLen; memcpy(&nameLen, p + cur, 2);
            uint8_t isDir = p[cur + 2];
            uint64_t size; memcpy(&size, p + cur + 3, 8);
            uint64_t mtime; memcpy(&mtime, p + cur + 11, 8);
            uint64_t ctime; memcpy(&ctime, p + cur + 19, 8);
            uint64_t atime; memcpy(&atime, p + cur + 27, 8);
            
            cur += 39;
            if (cur + nameLen > total) break;
            std::string name((char*)(p + cur), nameLen); cur += nameLen;
            
            struct fuse_stat st; memset(&st, 0, sizeof(st));
            st.st_mode = isDir ? (S_IFDIR | 0777) : (S_IFREG | 0777);
            st.st_size = size;
            st.st_mtim.tv_sec = mtime; st.st_ctim.tv_sec = ctime; st.st_atim.tv_sec = atime;
            
            if (filler(buf, name.c_str(), &st, 0)) break;
        }
        return 0;
    } catch (...) { return -EIO; }
}

static int vfs_read(const char* path, char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        auto res = DispatchRpc(VfsOpcode::OP_READ, full_path, offset, size, {});
        if (res.size() <= sizeof(VfsPacketHeader)) return -EIO;
        
        size_t data_sz = res.size() - sizeof(VfsPacketHeader);
        if (data_sz > size) data_sz = size;
        memcpy(buf, res.data() + sizeof(VfsPacketHeader), data_sz);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        ZHI_LOG_DEBUG("[FUSE-READ] " + std::string(path) + " | Size: " + std::to_string(size) + "B | Offset: " + std::to_string(offset) + " | Total Time: " + std::to_string(duration_ms) + "ms");
        
        return data_sz;
    } catch (...) { return -EIO; }
}

static int vfs_write(const char* path, const char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        RemoveFromRAMCache(full_path);
        std::vector<uint8_t> data(buf, buf + size);
        
        // 🔥 BẬT CỜ BẤT ĐỒNG BỘ ĐỂ GHI FILE NHANH CHÓNG MẶT
        auto res = DispatchRpc(VfsOpcode::OP_WRITE, full_path, offset, 0, data, true);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        ZHI_LOG_DEBUG("[FUSE-WRITE] " + std::string(path) + " | Size: " + std::to_string(size) + "B | Offset: " + std::to_string(offset) + " | Total Time: " + std::to_string(duration_ms) + "ms");
        
        return res.empty() ? -EIO : size;
    } catch (...) { return -EIO; }
}

static int vfs_mkdir(const char* path, fuse_mode_t mode) {
    try {
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        auto res = DispatchRpc(VfsOpcode::OP_MKDIR, full_path, 0, 0, {});
        return res.empty() ? -EIO : 0;
    } catch (...) { return -EIO; }
}

static int vfs_unlink(const char* path) {
    try {
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        RemoveFromRAMCache(full_path);
        auto res = DispatchRpc(VfsOpcode::OP_DELETE, full_path, 0, 0, {});
        return res.empty() ? -EIO : 0;
    } catch (...) { return -EIO; }
}

static int vfs_rmdir(const char* path) { return vfs_unlink(path); }

static int vfs_truncate(const char* path, fuse_off_t size) {
    try {
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        RemoveFromRAMCache(full_path);
        auto res = DispatchRpc(VfsOpcode::OP_TRUNCATE, full_path, size, 0, {});
        return res.empty() ? -EIO : 0;
    } catch (...) { return -EIO; }
}

static int vfs_rename(const char* oldpath, const char* newpath) {
    try {
        std::string full_old = g_remote_base; if (std::string(oldpath) != "/") full_old += oldpath;
        std::string full_new = g_remote_base; if (std::string(newpath) != "/") full_new += newpath;
        RemoveFromRAMCache(full_old); RemoveFromRAMCache(full_new);
        std::vector<uint8_t> new_p(full_new.begin(), full_new.end());
        auto res = DispatchRpc(VfsOpcode::OP_RENAME, full_old, 0, 0, new_p);
        return res.empty() ? -EIO : 0;
    } catch (...) { return -EIO; }
}

static int vfs_create(const char* path, fuse_mode_t mode, struct fuse_file_info* fi) {
    try {
        std::string full_path = g_remote_base;
        if (std::string(path) != "/") full_path += path;
        RemoveFromRAMCache(full_path); // Phải clear bộ nhớ đệm để Word lấy file mới
        auto res = DispatchRpc(VfsOpcode::OP_WRITE, full_path, 0, 0, {});
        return res.empty() ? -EIO : 0;
    } catch (...) { return -EIO; }
}

static int vfs_statfs(const char* path, struct fuse_statvfs* stbuf) {
    try {
        memset(stbuf, 0, sizeof(struct fuse_statvfs));
        stbuf->f_bsize = 4096;
        stbuf->f_frsize = 4096;
        stbuf->f_blocks = 268435456ULL; // 1 TB Total
        stbuf->f_bfree = 131072000ULL;  // ~500 GB Free
        stbuf->f_bavail = 131072000ULL; // ~500 GB Avail
        return 0;
    } catch (...) { return -EIO; }
}

// 🔥 CÁC HÀM ĐÁNH LỪA MS WORD: CHOWN, CHMOD, UTIMENS VÀ ĐẶC BIẾT LÀ FSYNC (LƯU ĐÈ)
static int vfs_chmod(const char* path, fuse_mode_t mode) { return 0; }
static int vfs_chown(const char* path, fuse_uid_t uid, fuse_gid_t gid) { return 0; }
static int vfs_utimens(const char* path, const struct fuse_timespec tv[2]) { return 0; }
static int vfs_fsync(const char* path, int isdatasync, struct fuse_file_info* fi) { return 0; }
static int vfs_open(const char* path, struct fuse_file_info* fi) { return 0; }
static int vfs_flush(const char* path, struct fuse_file_info* fi) { return 0; }
static int vfs_release(const char* path, struct fuse_file_info* fi) { return 0; }

static struct fuse_operations vfs_oper = {};

int FuseDriver::start_fuse(const std::string& mountpoint, const std::string& remote_base, bool use_kcp) {
    g_remote_base = remote_base;
    memset(&vfs_oper, 0, sizeof(vfs_oper));
    vfs_oper.getattr = vfs_getattr;
    vfs_oper.mkdir = vfs_mkdir;
    vfs_oper.unlink = vfs_unlink;
    vfs_oper.rmdir = vfs_rmdir;
    vfs_oper.rename = vfs_rename;
    vfs_oper.truncate = vfs_truncate;
    vfs_oper.open = vfs_open;
    vfs_oper.read = vfs_read;
    vfs_oper.write = vfs_write;
    vfs_oper.flush = vfs_flush;
    vfs_oper.release = vfs_release;
    vfs_oper.readdir = vfs_readdir;
    vfs_oper.create = vfs_create;
    vfs_oper.statfs = vfs_statfs;
    vfs_oper.chmod = vfs_chmod;      // 🔥 Phá bẫy Read-Only
    vfs_oper.chown = vfs_chown;      // 🔥 Phá bẫy Read-Only
    vfs_oper.utimens = vfs_utimens;  // 🔥 Phá bẫy Read-Only
    vfs_oper.fsync = vfs_fsync;      // 🔥 Phá bẫy Cấm Lưu Đè (Save)

    // 🔥 VÁ TỬ HUYỆT WINDOWS: Ép max_read và max_write xuống 64KB (65536).
    char* argv[] = { 
        (char*)"zhiauth_fuse", 
        (char*)"-f", 
        (char*)"-o", (char*)"max_read=65536,max_write=65536,ThreadCount=16,FileInfoTimeout=-1,uid=-1,gid=-1,umask=000", 
        (char*)mountpoint.c_str() 
    };
    ZHI_LOG_INFO("[FUSE-DRIVER] Kich hoat O dia ao C++ tai o dia: " + mountpoint + " (MS Office Unlock & RAM-Cache Enabled)");
    return fuse_main(5, argv, &vfs_oper, (void*)use_kcp);
}
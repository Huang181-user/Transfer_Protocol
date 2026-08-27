#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 28

#include <winsock2.h>
#include <windows.h>
#include <fuse.h>
#include "vfs/fuse_driver.h"
#include "rpc_client/vfs_packet.h"
#include "rpc_client/vfs_client.h"
#include "rpc_quic/msquic_client.h"
#include "common/logger.h"

#include <string>
#include <vector>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <mutex>

extern VfsClient* g_vfs_client;
std::atomic<uint32_t> g_req_id{1};

struct MountContext {
    std::string remote_base;
    bool use_kcp;
};

struct WriteBuffer {
    std::mutex mu;
    std::string realPath;
    uint64_t startOffset;
    std::vector<uint8_t> data;
    bool isFlushing;
    bool use_kcp;
};

static std::unordered_map<std::string, std::shared_ptr<WriteBuffer>> g_write_buffers;
static std::mutex g_wb_mtx;

struct FileMeta { bool is_dir; uint64_t size; uint32_t mode; time_t exp; };
static std::unordered_map<std::string, FileMeta> g_ram_cache;
static std::mutex g_cache_mtx;

std::string get_vfs_path(const std::string& remote_base, const char* reqPath) {
    std::string p = reqPath;
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.empty() || p.front() != '/') p = "/" + p;
    std::string base = remote_base;
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + p;
}

std::vector<uint8_t> DispatchRpc(VfsOpcode opcode, const std::string& path, uint64_t offset, uint32_t reqLen, bool use_kcp, const std::vector<uint8_t>& in_data = {}) {
    uint32_t req_id = g_req_id.fetch_add(1);
    uint32_t data_len = (opcode == VfsOpcode::OP_READ) ? reqLen : in_data.size();
    std::vector<uint8_t> req(sizeof(VfsPacketHeader) + path.size() + in_data.size());
    uint32_t client_id = g_vfs_client ? g_vfs_client->get_client_id() : 0;
    
    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req.data());
    hdr->magic = 0x5A484941; hdr->opcode = opcode; 
    hdr->session_id = ((uint64_t)client_id << 32) | (uint64_t)req_id;
    hdr->offset = offset; hdr->data_len = data_len; hdr->path_len = path.size();

    memcpy(req.data() + sizeof(VfsPacketHeader), path.data(), path.size());
    if (!in_data.empty()) memcpy(req.data() + sizeof(VfsPacketHeader) + path.size(), in_data.data(), in_data.size());

    if (use_kcp && g_vfs_client) {
        auto res = g_vfs_client->send_rpc_sync(req, req_id);
        if (!res.empty() && res.size() >= sizeof(VfsPacketHeader)) {
            if (reinterpret_cast<VfsPacketHeader*>(res.data())->opcode != VfsOpcode::OP_ERROR) return res;
        }
    }
    auto fallback_res = MsQuicClient::send_vfs_sync(req, req_id);
    if (!fallback_res.empty()) return fallback_res;
    return {};
}

void FlushChunkAsync(std::shared_ptr<WriteBuffer> wb, bool forceAll) {
    while (true) {
        wb->mu.lock();
        if (wb->data.empty() || (!forceAll && wb->data.size() < 131072)) {
            wb->isFlushing = false;
            wb->mu.unlock();
            return;
        }

        size_t toSendLen = wb->data.size();
        if (toSendLen > 131072) toSendLen = 131072;

        std::vector<uint8_t> chunk(wb->data.begin(), wb->data.begin() + toSendLen);
        uint64_t currentOffset = wb->startOffset;

        wb->startOffset += toSendLen;
        wb->data.erase(wb->data.begin(), wb->data.begin() + toSendLen);
        wb->mu.unlock();

        DispatchRpc(VfsOpcode::OP_WRITE, wb->realPath, currentOffset, 0, wb->use_kcp, chunk);
    }
}

static int vfs_getattr(const char* path, struct fuse_stat* stbuf) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    memset(stbuf, 0, sizeof(struct fuse_stat));
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    
    if (full_path == ctx->remote_base || full_path == ctx->remote_base + "/") {
        stbuf->st_mode = 0040000 | 0777; // S_IFDIR
        stbuf->st_nlink = 2;
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(g_cache_mtx);
        auto it = g_ram_cache.find(full_path);
        if (it != g_ram_cache.end() && time(NULL) < it->second.exp) {
            stbuf->st_mode = it->second.is_dir ? (0040000 | 0777) : (0100000 | 0777);
            stbuf->st_nlink = it->second.is_dir ? 2 : 1;
            stbuf->st_size = it->second.size;
            return 0;
        }
    }

    auto res = DispatchRpc(VfsOpcode::OP_STAT, full_path, 0, 0, ctx->use_kcp);
    if (res.size() < sizeof(VfsPacketHeader) + 37) return -2; // -ENOENT

    uint8_t* payload = res.data() + sizeof(VfsPacketHeader);
    uint64_t size; memcpy(&size, payload, 8);
    uint8_t isDir = payload[8];

    stbuf->st_mode = isDir ? (0040000 | 0777) : (0100000 | 0777); 
    stbuf->st_nlink = isDir ? 2 : 1;
    stbuf->st_size = size;
    return 0;
}

static int vfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, fuse_off_t offset, struct fuse_file_info* fi) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    std::string full_path = get_vfs_path(ctx->remote_base, path);
    auto res = DispatchRpc(VfsOpcode::OP_LIST, full_path, 0, 0, ctx->use_kcp);
    if (res.size() <= sizeof(VfsPacketHeader)) return 0;

    uint8_t* p = res.data() + sizeof(VfsPacketHeader);
    size_t total = res.size() - sizeof(VfsPacketHeader); size_t cur = 0;

    while (cur + 15 <= total) {
        uint16_t nameLen; memcpy(&nameLen, p + cur, 2); uint8_t isDir = p[cur + 2];
        uint64_t fileSize; memcpy(&fileSize, p + cur + 3, 8);
        cur += 15; if (cur + nameLen > total) break;
        
        std::string name((char*)(p + cur), nameLen); cur += nameLen;
        
        struct fuse_stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = isDir ? (0040000 | 0777) : (0100000 | 0777);
        st.st_size = fileSize;

        filler(buf, name.c_str(), &st, 0);

        std::string childPath = full_path;
        if (childPath.back() != '/') childPath += "/";
        childPath += name;
        std::lock_guard<std::mutex> lock(g_cache_mtx);
        g_ram_cache[childPath] = {isDir == 1, fileSize, 0, time(NULL) + 60};
    }
    return 0;
}

static int vfs_read(const char* path, char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    auto res = DispatchRpc(VfsOpcode::OP_READ, full_path, offset, size, ctx->use_kcp);
    if (res.size() <= sizeof(VfsPacketHeader)) return -5; // -EIO
    
    size_t data_sz = res.size() - sizeof(VfsPacketHeader); 
    memcpy(buf, res.data() + sizeof(VfsPacketHeader), data_sz);
    return data_sz;
}

static int vfs_write(const char* path, const char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    
    { std::lock_guard<std::mutex> lock(g_cache_mtx); g_ram_cache.erase(full_path); }

    if (size == 0) {
        DispatchRpc(VfsOpcode::OP_WRITE, full_path, offset, 0, ctx->use_kcp, {});
        return 0;
    }

    std::shared_ptr<WriteBuffer> wb;
    {
        std::lock_guard<std::mutex> lock(g_wb_mtx);
        if (g_write_buffers.find(full_path) == g_write_buffers.end()) {
            g_write_buffers[full_path] = std::make_shared<WriteBuffer>();
            g_write_buffers[full_path]->realPath = full_path;
            g_write_buffers[full_path]->startOffset = offset;
            g_write_buffers[full_path]->use_kcp = ctx->use_kcp;
            g_write_buffers[full_path]->isFlushing = false;
        }
        wb = g_write_buffers[full_path];
    }

    bool shouldFlush = false;
    wb->mu.lock();
    wb->data.insert(wb->data.end(), buf, buf + size);
    if (wb->data.size() >= 131072 && !wb->isFlushing) {
        wb->isFlushing = true;
        shouldFlush = true;
    }
    wb->mu.unlock();

    if (shouldFlush) {
        std::thread(FlushChunkAsync, wb, false).detach();
    }
    return size;
}

static int vfs_flush(const char* path, struct fuse_file_info* fi) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    std::shared_ptr<WriteBuffer> wb;
    {
        std::lock_guard<std::mutex> lock(g_wb_mtx);
        if (g_write_buffers.find(full_path) != g_write_buffers.end()) wb = g_write_buffers[full_path];
    }
    if (wb) {
        wb->mu.lock();
        if (!wb->isFlushing) {
            wb->isFlushing = true;
            wb->mu.unlock();
            FlushChunkAsync(wb, true);
        } else {
            while (wb->data.size() > 0) { wb->mu.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); wb->mu.lock(); }
            wb->mu.unlock();
        }
    }
    return 0;
}

static int vfs_release(const char* path, struct fuse_file_info* fi) {
    vfs_flush(path, fi);
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    std::lock_guard<std::mutex> lock(g_wb_mtx);
    g_write_buffers.erase(full_path);
    return 0;
}

static int vfs_mkdir(const char* path, fuse_mode_t mode) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    auto res = DispatchRpc(VfsOpcode::OP_MKDIR, full_path, 0, 0, ctx->use_kcp);
    return res.empty() ? -5 : 0;
}

static int vfs_unlink(const char* path) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    { std::lock_guard<std::mutex> lock(g_cache_mtx); g_ram_cache.erase(full_path); }
    auto res = DispatchRpc(VfsOpcode::OP_DELETE, full_path, 0, 0, ctx->use_kcp);
    return res.empty() ? -5 : 0;
}

static int vfs_rmdir(const char* path) { return vfs_unlink(path); }

static int vfs_rename(const char* oldpath, const char* newpath) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_old = get_vfs_path(ctx->remote_base, oldpath);
    std::string full_new = get_vfs_path(ctx->remote_base, newpath);
    { std::lock_guard<std::mutex> lock(g_cache_mtx); g_ram_cache.erase(full_old); g_ram_cache.erase(full_new); }
    std::vector<uint8_t> new_p(full_new.begin(), full_new.end());
    auto res = DispatchRpc(VfsOpcode::OP_RENAME, full_old, 0, 0, ctx->use_kcp, new_p);
    return res.empty() ? -5 : 0;
}

static int vfs_truncate(const char* path, fuse_off_t size) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    { std::lock_guard<std::mutex> lock(g_cache_mtx); g_ram_cache.erase(full_path); }
    auto res = DispatchRpc(VfsOpcode::OP_TRUNCATE, full_path, size, 0, ctx->use_kcp);
    return res.empty() ? -5 : 0;
}

static int vfs_create(const char* path, fuse_mode_t mode, struct fuse_file_info* fi) {
    MountContext* ctx = (MountContext*)fuse_get_context()->private_data;
    std::string full_path = get_vfs_path(ctx->remote_base, path);
    auto res = DispatchRpc(VfsOpcode::OP_WRITE, full_path, 0, 0, ctx->use_kcp);
    return res.empty() ? -5 : 0;
}

static int vfs_open(const char* path, struct fuse_file_info* fi) { return 0; }
static int vfs_fsync(const char* path, int isdatasync, struct fuse_file_info* fi) { return vfs_flush(path, fi); }

static struct fuse_operations vfs_oper;

// 🔥 FIX CHỮ KÝ HÀM: Đã sửa lại thành `int FuseDriver::start_fuse(const std::string&, const std::string&, bool)`
int FuseDriver::start_fuse(const std::string& mountpoint, const std::string& remote_base, bool use_kcp) {
    MountContext* ctx = new MountContext{remote_base, use_kcp};
    memset(&vfs_oper, 0, sizeof(vfs_oper));
    vfs_oper.getattr = vfs_getattr;
    vfs_oper.readdir = vfs_readdir;
    vfs_oper.read = vfs_read;
    vfs_oper.write = vfs_write;
    vfs_oper.flush = vfs_flush;
    vfs_oper.release = vfs_release;
    vfs_oper.fsync = vfs_fsync;
    vfs_oper.mkdir = vfs_mkdir;
    vfs_oper.unlink = vfs_unlink;
    vfs_oper.rmdir = vfs_rmdir;
    vfs_oper.rename = vfs_rename;
    vfs_oper.truncate = vfs_truncate;
    vfs_oper.create = vfs_create;
    vfs_oper.open = vfs_open;

    std::vector<char*> args;
    args.push_back(strdup("zhiauth_vfs.exe"));
    args.push_back(strdup("-f"));
    args.push_back(strdup("-ouid=-1"));
    args.push_back(strdup("-ogid=-1"));
    
    std::string fsname = use_kcp ? "-oFileSystemName=ZhiAuth-KCP" : "-oFileSystemName=ZhiAuth-QUIC";
    std::string volname = use_kcp ? "-ovolname=ZhiAuth_KCP" : "-ovolname=ZhiAuth_QUIC";
    args.push_back(strdup(fsname.c_str()));
    args.push_back(strdup(volname.c_str()));
    args.push_back(strdup(mountpoint.c_str()));
    
    int ret = fuse_main(args.size(), args.data(), &vfs_oper, (void*)ctx);
    
    for (char* arg : args) free(arg);
    delete ctx;
    return ret;
}

#include "rpc_vfs/vfs_server.h"
#include "rpc_vfs/vfs_packet.h"
#include "rpc_vfs/crypto_box.h"
#include "common/logger.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sys/sysinfo.h>
#include <queue>
#include <functional>
#include <vector>
#include <cerrno>

using asio::ip::udp;

static std::unordered_map<std::string, std::string> g_kcp_ip_uds_map;
static std::mutex g_kcp_ip_uds_mutex;
static std::queue<std::function<void()>> g_task_queue;
static std::mutex g_task_mutex;
static std::condition_variable g_task_cv;
static bool g_task_running = false;
static std::vector<std::thread> g_worker_pool;

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

void vfs_register_ip_uds(const std::string& ip, const std::string& uds_path) {
    std::lock_guard<std::mutex> lock(g_kcp_ip_uds_mutex); g_kcp_ip_uds_map[ip] = uds_path;
}
std::string vfs_get_uds_by_ip(const std::string& ip) {
    std::lock_guard<std::mutex> lock(g_kcp_ip_uds_mutex); return g_kcp_ip_uds_map.count(ip) ? g_kcp_ip_uds_map[ip] : "";
}

VfsServer::VfsServer(uint16_t port, const std::string& master_sym_key, int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) 
    : socket_(io_context_, udp::endpoint(udp::v4(), port)), is_running_(false), recv_buffer_(65536),
      master_sym_key_(master_sym_key), nodelay_(nodelay), interval_(interval), resend_(resend), nc_(nc), snd_wnd_(snd_wnd), rcv_wnd_(rcv_wnd) {
    try { socket_.set_option(asio::socket_base::receive_buffer_size(16777216)); socket_.set_option(asio::socket_base::send_buffer_size(16777216)); } catch (...) {}
}
VfsServer::~VfsServer() { stop(); }

uint64_t VfsServer::get_last_active_time(const std::string& ip) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    uint64_t max_time = 0;
    for (const auto& pair : sessions_) {
        if (pair.first.rfind(ip + ":", 0) == 0) { 
            if (pair.second.last_active_time > max_time) max_time = pair.second.last_active_time;
        }
    }
    return max_time;
}

static void start_task_worker() {
    if (g_task_running) return; g_task_running = true; size_t num_threads = 8;
    for (size_t i = 0; i < num_threads; ++i) {
        g_worker_pool.emplace_back([]() {
            while (g_task_running) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(g_task_mutex);
                    g_task_cv.wait(lock, []{ return !g_task_queue.empty() || !g_task_running; });
                    if (!g_task_running && g_task_queue.empty()) break;
                    task = std::move(g_task_queue.front()); g_task_queue.pop();
                }
                if (task) task();
            }
        });
    }
}
static void stop_task_worker() {
    g_task_running = false; g_task_cv.notify_all();
    for (auto& th : g_worker_pool) { if (th.joinable()) th.join(); }
    g_worker_pool.clear();
}

void VfsServer::start() {
    if (is_running_) return; is_running_ = true; sessions_.reserve(100); start_task_worker();
    ZHI_LOG_INFO("Kích nổ Gateway UDP Proxy (Blocking I/O Mode) - HYPER KCP Engine Operational");
    io_thread_ = std::thread(&VfsServer::receive_loop, this);
    timer_thread_ = std::thread(&VfsServer::kcp_update_loop, this);
}
void VfsServer::stop() {
    is_running_ = false; socket_.close();
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join(); stop_task_worker();
}

int VfsServer::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    auto* ctx = static_cast<KcpUserContext*>(user);
    if (ctx && ctx->server) { std::error_code ec; ctx->server->socket_.send_to(asio::buffer(buf, len), ctx->remote_endpoint, 0, ec); }
    return 0;
}

// ======================================================================
// 🚀 1. VÒNG LẶP HÚT CẠN (Đã mở khóa Đa Luồng)
// ======================================================================
void VfsServer::receive_loop() {
    while (is_running_) {
        std::error_code ec;
        size_t bytes_recvd = socket_.receive_from(asio::buffer(recv_buffer_), sender_endpoint_, 0, ec);
        
        if (ec || bytes_recvd == 0) {
            if (ec == asio::error::would_block || ec == asio::error::try_again) continue;
            break; 
        }

        std::string ip = sender_endpoint_.address().to_string(); if(ip.find("::ffff:")==0) ip = ip.substr(7); std::string client_key = ip + ":" + std::to_string(sender_endpoint_.port());
        
        std::unique_lock<std::mutex> lock(session_mutex_);
        auto it = sessions_.find(client_key);
        if (it == sessions_.end()) {
            KcpSession new_session;
            new_session.user_ctx = std::make_shared<KcpUserContext>(KcpUserContext{this, sender_endpoint_});
            new_session.kcp_cb = ikcp_create(0x11223344, new_session.user_ctx.get());
            new_session.kcp_cb->output = kcp_output_callback;
            
            ikcp_nodelay(new_session.kcp_cb, nodelay_, interval_, resend_, nc_); 
            ikcp_wndsize(new_session.kcp_cb, snd_wnd_, rcv_wnd_);
            ikcp_setmtu(new_session.kcp_cb, 1350); 
            new_session.kcp_cb->rx_minrto = 10; 
            new_session.kcp_cb->dead_link = 200;
            new_session.uds_path = vfs_get_uds_by_ip(ip); if(new_session.uds_path.empty()) ZHI_LOG_WARN("[KCP-ENGINE] IP chưa Knocking: " + ip);
            
            // 🔥 KHỞI TẠO TÀI SẢN RIÊNG CHO SESSION
            new_session.kcp_mtx = std::make_shared<std::mutex>();
            new_session.uds_fd = std::make_shared<int>(-1);

            sessions_[client_key] = new_session; 
            it = sessions_.find(client_key);
        }
        
        it->second.last_active_time = time(NULL);
        KcpSession current_session = it->second; // Bốc dữ liệu ra
        lock.unlock(); // 🔥 MỞ KHÓA GLOBAL NGAY VÀ LUÔN CHO LUỒNG KHÁC VÀO!

        // Chỉ khóa KCP của riêng thằng Client này:
        std::lock_guard<std::mutex> kcp_lock(*current_session.kcp_mtx);
        ikcp_input(current_session.kcp_cb, reinterpret_cast<const char*>(recv_buffer_.data()), bytes_recvd);
        
        int len;
        while ((len = ikcp_peeksize(current_session.kcp_cb)) > 0) {
            std::vector<uint8_t> encrypted_payload(len);
            ikcp_recv(current_session.kcp_cb, reinterpret_cast<char*>(encrypted_payload.data()), len);
            
            {
                std::lock_guard<std::mutex> tlock(g_task_mutex);
                // Vứt vào hàng đợi cho 8 Worker xé xác:
                g_task_queue.push([this, current_session, encrypted_payload]() { 
                    process_kcp_payload(current_session, encrypted_payload); 
                });
            }
            g_task_cv.notify_one();
        }
    }
}

// ======================================================================
// 🚀 2. WORKER XỬ LÝ (Tái sử dụng UDS Socket, Không đóng ống)
// ======================================================================
void VfsServer::process_kcp_payload(KcpSession session_copy, std::vector<uint8_t> encrypted_payload) {
    std::vector<uint8_t> plaintext;
    if (!CryptoBox::decrypt_payload(encrypted_payload, master_sym_key_, plaintext)) return;
    if (plaintext.size() < sizeof(VfsPacketHeader)) return;

    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
    if (hdr->opcode == VfsOpcode::OP_PING) {
        VfsPacketHeader res_hdr = *hdr;
        std::vector<uint8_t> response_payload(sizeof(VfsPacketHeader));
        std::memcpy(response_payload.data(), &res_hdr, sizeof(VfsPacketHeader));
        std::vector<uint8_t> final_encrypted;
        if (CryptoBox::encrypt_payload(response_payload, master_sym_key_, final_encrypted)) {
            std::lock_guard<std::mutex> sess_lock(session_mutex_);
            ikcp_send(session_copy.kcp_cb, reinterpret_cast<const char*>(final_encrypted.data()), final_encrypted.size());
            ikcp_flush(session_copy.kcp_cb);
        }
        return;
    }

    if (session_copy.uds_path.empty()) return;

    // 🔥 MỖI WORKER MỞ 1 KẾT NỐI UDS ĐỘC LẬP (Tránh tuyệt đối lỗi trộn luồng stream)
    int uds_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds_fd < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, session_copy.uds_path.c_str(), sizeof(addr.sun_path) - 1);

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(uds_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(uds_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(uds_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(uds_fd);
        return;
    }

    uint32_t req_sz = plaintext.size();
    if (!send_all(uds_fd, &req_sz, 4) || !send_all(uds_fd, plaintext.data(), plaintext.size())) {
        close(uds_fd);
        return;
    }

    uint32_t resp_sz = 0;
    if (!recv_all(uds_fd, &resp_sz, 4)) {
        close(uds_fd);
        return;
    }

    std::vector<uint8_t> response_payload(resp_sz);
    if (!recv_all(uds_fd, response_payload.data(), resp_sz)) {
        close(uds_fd);
        return;
    }
    close(uds_fd);

    std::vector<uint8_t> final_encrypted;
    if (CryptoBox::encrypt_payload(response_payload, master_sym_key_, final_encrypted)) {
        std::lock_guard<std::mutex> sess_lock(session_mutex_);
        ikcp_send(session_copy.kcp_cb, reinterpret_cast<const char*>(final_encrypted.data()), final_encrypted.size());
        ikcp_flush(session_copy.kcp_cb);
    }
}

// ======================================================================
// 🚀 3. ÉP XUNG CLOCK: BƠM MÁU 1MS (Thay vì 5ms)
// ======================================================================
void VfsServer::kcp_update_loop() {
    while (is_running_) {
        // AI Training cần I/O siêu tốc, hạ delay xuống 1ms!
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
        uint32_t current_clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::lock_guard<std::mutex> lock(session_mutex_); // Khóa map để duyệt
        for (auto& pair : sessions_) {
            std::lock_guard<std::mutex> kcp_lock(*pair.second.kcp_mtx); // Khóa KCP nội bộ
            ikcp_update(pair.second.kcp_cb, current_clock);
        }
    }
}
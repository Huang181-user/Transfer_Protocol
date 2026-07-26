#include "rpc_vfs/vfs_server.h"
#include "rpc_vfs/vfs_packet.h"
#include "rpc_vfs/crypto_box.h"
#include "common/logger.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <sstream>
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

static thread_local int t_persistent_uds_fd = -1;

static int get_persistent_uds_fd(const std::string& uds_path) {
    if (t_persistent_uds_fd != -1) {
        char check_buf;
        int res = recv(t_persistent_uds_fd, &check_buf, 1, MSG_PEEK | MSG_DONTWAIT);
        if (res == 0 || (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(t_persistent_uds_fd); t_persistent_uds_fd = -1;
        }
    }
    if (t_persistent_uds_fd == -1) {
        t_persistent_uds_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (t_persistent_uds_fd < 0) return -1;
        struct sockaddr_un addr; memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX; strncpy(addr.sun_path, uds_path.c_str(), sizeof(addr.sun_path) - 1);
        struct timeval tv; tv.tv_sec = 120; tv.tv_usec = 0;
        setsockopt(t_persistent_uds_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(t_persistent_uds_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        if (connect(t_persistent_uds_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(t_persistent_uds_fd); t_persistent_uds_fd = -1; return -1;
        }
    }
    return t_persistent_uds_fd;
}

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

static size_t getSystemMaxRmemServer() {
    std::ifstream file("/proc/sys/net/core/rmem_max"); size_t rmem = 0;
    if (file >> rmem && rmem > 0) return rmem; return 1073741824;
}
static size_t calculateAbsoluteMaxServerBuffer() {
    struct sysinfo info; size_t free_ram_buf = 1073741824;
    if (sysinfo(&info) == 0) { free_ram_buf = info.freeram * info.mem_unit * 0.80; }
    size_t sys_rmem = getSystemMaxRmemServer();
    size_t target = std::max(free_ram_buf, sys_rmem);
    return std::clamp(target, static_cast<size_t>(512 * 1024 * 1024), static_cast<size_t>(4096ULL * 1024 * 1024));
}

void vfs_register_ip_uds(const std::string& ip, const std::string& uds_path) {
    std::lock_guard<std::mutex> lock(g_kcp_ip_uds_mutex); g_kcp_ip_uds_map[ip] = uds_path;
}
std::string vfs_get_uds_by_ip(const std::string& ip) {
    std::lock_guard<std::mutex> lock(g_kcp_ip_uds_mutex); return g_kcp_ip_uds_map.count(ip) ? g_kcp_ip_uds_map[ip] : "";
}

// 🔥 NẠP MASTER KEY VÀ PORT TỪ NGOÀI VÀO, XÓA SẠCH HARDCODE
VfsServer::VfsServer(uint16_t port, const std::string& master_sym_key) : socket_(io_context_, udp::endpoint(udp::v4(), port)), is_running_(false), recv_buffer_(65536) {
    master_sym_key_ = master_sym_key;
    size_t auto_buf = calculateAbsoluteMaxServerBuffer();
    try { socket_.set_option(asio::socket_base::receive_buffer_size(auto_buf)); socket_.set_option(asio::socket_base::send_buffer_size(auto_buf)); } catch (...) {}
}
VfsServer::~VfsServer() { stop(); }

static void start_task_worker() {
    if (g_task_running) return; g_task_running = true; size_t num_threads = 16;
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
            if (t_persistent_uds_fd != -1) { close(t_persistent_uds_fd); t_persistent_uds_fd = -1; }
        });
    }
}
static void stop_task_worker() {
    g_task_running = false; g_task_cv.notify_all();
    for (auto& th : g_worker_pool) { if (th.joinable()) th.join(); }
    g_worker_pool.clear();
}

void VfsServer::start() {
    if (is_running_) return; is_running_ = true; sessions_.reserve(1000); start_task_worker();
    ZHI_LOG_INFO("Kích nổ Gateway UDP Proxy (Port " + std::to_string(socket_.local_endpoint().port()) + ") - HYPER KCP Engine Operational");
    io_thread_ = std::thread([this]() { receive_loop(); io_context_.run(); });
    timer_thread_ = std::thread(&VfsServer::kcp_update_loop, this);
}
void VfsServer::stop() {
    is_running_ = false; io_context_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join(); stop_task_worker();
}

void VfsServer::receive_loop() {
    socket_.async_receive_from(asio::buffer(recv_buffer_), sender_endpoint_, [this](std::error_code ec, std::size_t bytes_recvd) {
        if (!ec && bytes_recvd > 0) {
            std::string client_key = sender_endpoint_.address().to_string() + ":" + std::to_string(sender_endpoint_.port());
            std::lock_guard<std::mutex> lock(session_mutex_);
            auto it = sessions_.find(client_key);
            if (it == sessions_.end()) {
                KcpSession new_session; new_session.kcp_cb = ikcp_create(0x11223344, this);
                new_session.kcp_cb->output = kcp_output_callback; new_session.remote_endpoint = sender_endpoint_;
                ikcp_nodelay(new_session.kcp_cb, 1, 10, 2, 1); ikcp_wndsize(new_session.kcp_cb, 65535, 65535);
                ikcp_setmtu(new_session.kcp_cb, 1350); new_session.kcp_cb->rx_minrto = 30; new_session.kcp_cb->dead_link = 200;
                new_session.uds_path = vfs_get_uds_by_ip(sender_endpoint_.address().to_string());
                new_session.uds_fd = -1; sessions_[client_key] = new_session; it = sessions_.find(client_key);
            }
            it->second.last_active_time = std::chrono::steady_clock::now().time_since_epoch().count();
            ikcp_input(it->second.kcp_cb, reinterpret_cast<const char*>(recv_buffer_.data()), bytes_recvd);
            
            int len;
            while ((len = ikcp_peeksize(it->second.kcp_cb)) > 0) {
                std::vector<uint8_t> encrypted_payload(len);
                ikcp_recv(it->second.kcp_cb, reinterpret_cast<char*>(encrypted_payload.data()), len);
                KcpSession* sess_ptr = &(it->second);
                {
                    std::lock_guard<std::mutex> tlock(g_task_mutex);
                    g_task_queue.push([this, sess_ptr, encrypted_payload]() { process_kcp_payload(*sess_ptr, encrypted_payload); });
                }
                g_task_cv.notify_one();
            }
        }
        if (is_running_) receive_loop();
    });
}

int VfsServer::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsServer* server = static_cast<VfsServer*>(user);
    for (auto& pair : server->sessions_) {
        if (pair.second.kcp_cb == kcp) {
            std::error_code ec; server->socket_.send_to(asio::buffer(buf, len), pair.second.remote_endpoint, 0, ec); return 0;
        }
    }
    return 0;
}

void VfsServer::process_kcp_payload(KcpSession& session, const std::vector<uint8_t>& encrypted_payload) {
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> plaintext;
    if (!CryptoBox::decrypt_payload(encrypted_payload, master_sym_key_, plaintext)) return;
    if (session.uds_path.empty()) return;

    int uds_fd = get_persistent_uds_fd(session.uds_path);
    if (uds_fd < 0) { ZHI_LOG_ERR("❌ [UDS-FAIL] Thread Worker không thể kết nối tới Socket nội bộ: " + session.uds_path); return; }

    uint32_t req_sz = plaintext.size();
    if (!send_all(uds_fd, &req_sz, 4) || !send_all(uds_fd, plaintext.data(), plaintext.size())) {
        if (t_persistent_uds_fd != -1) { close(t_persistent_uds_fd); t_persistent_uds_fd = -1; }
        ZHI_LOG_ERR("❌ [UDS-WRITE-FAIL] Lỗi gửi dữ liệu vào đường ống."); return;
    }

    uint32_t resp_sz = 0;
    if (!recv_all(uds_fd, &resp_sz, 4)) {
        if (t_persistent_uds_fd != -1) { close(t_persistent_uds_fd); t_persistent_uds_fd = -1; }
        ZHI_LOG_ERR("❌ [UDS-READ-FAIL] Lỗi không nhận được phản hồi Header từ Worker."); return;
    }

    std::vector<uint8_t> response_payload(resp_sz);
    if (!recv_all(uds_fd, response_payload.data(), resp_sz)) {
        if (t_persistent_uds_fd != -1) { close(t_persistent_uds_fd); t_persistent_uds_fd = -1; }
        ZHI_LOG_ERR("❌ [UDS-BODY-FAIL] Lỗi mất gói tin thân từ Worker."); return;
    }

    std::vector<uint8_t> final_encrypted;
    if (CryptoBox::encrypt_payload(response_payload, master_sym_key_, final_encrypted)) {
        std::lock_guard<std::mutex> sess_lock(session_mutex_);
        ikcp_send(session.kcp_cb, reinterpret_cast<const char*>(final_encrypted.data()), final_encrypted.size());
        ikcp_flush(session.kcp_cb);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double exec_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    if (exec_ms > 50.0) { ZHI_LOG_WARN("⚠️ [LATENCY-ALERT] VFS KCP Xử lý chậm bất thường: " + std::to_string(exec_ms) + " ms"); }
}

void VfsServer::kcp_update_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        uint32_t current_clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(session_mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) ikcp_update(it->second.kcp_cb, current_clock);
    }
}

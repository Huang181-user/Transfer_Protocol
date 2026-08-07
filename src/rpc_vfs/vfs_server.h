#ifndef ZHIAUTH_VFS_SERVER_H
#define ZHIAUTH_VFS_SERVER_H
#include <asio.hpp>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include "rpc_vfs/ikcp.h"

class VfsServer;

// Cấu trúc Context truyền trực tiếp vào KCP để bắn UDP không qua vòng lặp Duyệt Map
struct KcpUserContext {
    VfsServer* server;
    asio::ip::udp::endpoint remote_endpoint;
};

struct KcpSession {
    ikcpcb* kcp_cb;
    std::shared_ptr<KcpUserContext> user_ctx;
    uint64_t last_active_time;
    std::string uds_path;
};

class VfsServer {
public:
    VfsServer(uint16_t port, const std::string& master_sym_key, int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd);    ~VfsServer();
    void start();
    void stop();
    static int kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user);
    uint64_t get_last_active_time(const std::string& ip);
    
    // 🔥 FIX TỬ HUYỆT: io_context BẮT BUỘC PHẢI KHAI BÁO TRƯỚC socket ĐỂ KHỞI TẠO ĐÚNG TRÌNH TỰ
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;

private:
    void receive_loop();
    void kcp_update_loop();
    void process_kcp_payload(KcpSession session_copy, std::vector<uint8_t> encrypted_payload);
    
    int nodelay_, interval_, resend_, nc_, snd_wnd_, rcv_wnd_;
    asio::ip::udp::endpoint sender_endpoint_;
    std::vector<uint8_t> recv_buffer_;
    std::thread io_thread_;
    std::thread timer_thread_;
    std::atomic<bool> is_running_;
    std::unordered_map<std::string, KcpSession> sessions_;
    std::mutex session_mutex_;
    std::string master_sym_key_;
};
#endif

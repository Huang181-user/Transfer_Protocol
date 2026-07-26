#ifndef ZHIAUTH_VFS_SERVER_H
#define ZHIAUTH_VFS_SERVER_H
#include <asio.hpp>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include "rpc_vfs/ikcp.h"

struct KcpSession {
    ikcpcb* kcp_cb;
    asio::ip::udp::endpoint remote_endpoint;
    uint64_t last_active_time;
    std::string uds_path;
    int uds_fd;
};

class VfsServer {
public:
    // Nhận Master Key động từ Gateway
    VfsServer(uint16_t port, const std::string& master_sym_key);
    ~VfsServer();
    void start();
    void stop();
    static int kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user);
private:
    void receive_loop();
    void kcp_update_loop();
    void process_kcp_payload(KcpSession& session, const std::vector<uint8_t>& encrypted_payload);
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
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

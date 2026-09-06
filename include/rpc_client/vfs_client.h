#pragma once
#include <asio.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include "rpc_client/ikcp.h"

struct RpcContext {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> response;
    bool done = false;
};

class VfsClient {
public:
    // 🔥 Thêm const std::string& hwid vào cuối Constructor
    VfsClient(const std::string& server_ip, uint16_t port, const std::string& sym_key, int mtu,
              int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd, const std::string& hwid);
    ~VfsClient();
    bool start();
    void stop();
    std::vector<uint8_t> send_rpc_sync(const std::vector<uint8_t>& payload, uint32_t req_id);
    void send_rpc_async(const std::vector<uint8_t>& payload, uint32_t req_id);
    
    static int kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user);
    
    // 🔥 Thêm hàm này
    uint32_t get_client_id() const { return client_id_; }

private:
    void receive_loop();
    void kcp_update_loop();
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint server_endpoint_;
    std::vector<uint8_t> recv_buffer_;
    std::thread io_thread_;
    std::thread timer_thread_;
    std::atomic<bool> is_running_;
    ikcpcb* kcp_cb_;
    std::mutex kcp_mutex_;
    std::string sym_key_;
    int mtu_, nodelay_, interval_, resend_, nc_, snd_wnd_, rcv_wnd_;
    
    // 🔥 Thêm biến này vào cuối
    uint32_t client_id_;
    
    std::unordered_map<uint32_t, std::shared_ptr<RpcContext>> rpc_map_;
    std::mutex rpc_map_mutex_;
};

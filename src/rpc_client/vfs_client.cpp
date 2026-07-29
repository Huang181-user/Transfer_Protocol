#include "rpc_client/vfs_client.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_packet.h"
#include <iostream>
#include <chrono>
using asio::ip::udp;
VfsClient::VfsClient(const std::string& server_ip, uint16_t port, const std::string& sym_key, int mtu)
    : socket_(io_context_, udp::endpoint(udp::v4(), 0)), is_running_(false), recv_buffer_(65536), kcp_cb_(nullptr), sym_key_(sym_key), mtu_(mtu) {
    asio::ip::udp::resolver resolver(io_context_);
    server_endpoint_ = *resolver.resolve(udp::v4(), server_ip, std::to_string(port)).begin();
}
VfsClient::~VfsClient() { stop(); }
bool VfsClient::start() {
    if (is_running_) return true;
    is_running_ = true;
    kcp_cb_ = ikcp_create(0x11223344, this);
    kcp_cb_->output = kcp_output_callback;
    ikcp_nodelay(kcp_cb_, 1, 10, 2, 1);
    ikcp_wndsize(kcp_cb_, 8192, 8192);
    ikcp_setmtu(kcp_cb_, mtu_); 
    io_thread_ = std::thread([this]() { receive_loop(); io_context_.run(); });
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    return true;
}
void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false; io_context_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (kcp_cb_) { ikcp_release(kcp_cb_); kcp_cb_ = nullptr; }
}
int VfsClient::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsClient* client = static_cast<VfsClient*>(user);
    std::error_code ec; client->socket_.send_to(asio::buffer(buf, len), client->server_endpoint_, 0, ec);
    return 0;
}
void VfsClient::receive_loop() {
    socket_.async_receive_from(asio::buffer(recv_buffer_), server_endpoint_, [this](std::error_code ec, std::size_t bytes_recvd) {
        if (!ec && bytes_recvd > 0) {
            std::lock_guard<std::mutex> lock(kcp_mutex_);
            ikcp_input(kcp_cb_, reinterpret_cast<const char*>(recv_buffer_.data()), bytes_recvd);
            int len;
            while ((len = ikcp_peeksize(kcp_cb_)) > 0) {
                std::vector<uint8_t> encrypted_payload(len);
                ikcp_recv(kcp_cb_, reinterpret_cast<char*>(encrypted_payload.data()), len);
                std::vector<uint8_t> plaintext;
                if (CryptoBox::decrypt_payload(encrypted_payload, sym_key_, plaintext)) {
                    if (plaintext.size() >= sizeof(VfsPacketHeader)) {
                        VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
                        uint64_t req_id = hdr->session_id;
                        std::shared_ptr<RpcContext> ctx;
                        { std::lock_guard<std::mutex> map_lock(rpc_map_mutex_); auto it = rpc_map_.find(req_id); if (it != rpc_map_.end()) ctx = it->second; }
                        if (ctx) { std::lock_guard<std::mutex> lock(ctx->mtx); ctx->response = std::move(plaintext); ctx->done = true; ctx->cv.notify_one(); }
                    }
                }
            }
        }
        if (is_running_) receive_loop();
    });
}
void VfsClient::kcp_update_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        uint32_t current_clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(kcp_mutex_); if (kcp_cb_) ikcp_update(kcp_cb_, current_clock);
    }
}
std::vector<uint8_t> VfsClient::send_rpc_sync(const std::vector<uint8_t>& request_payload) {
    if (request_payload.size() < sizeof(VfsPacketHeader)) return {};
    const VfsPacketHeader* hdr = reinterpret_cast<const VfsPacketHeader*>(request_payload.data());
    uint64_t req_id = hdr->session_id;
    auto ctx = std::make_shared<RpcContext>();
    { std::lock_guard<std::mutex> map_lock(rpc_map_mutex_); rpc_map_[req_id] = ctx; }
    std::vector<uint8_t> encrypted_payload;
    if (!CryptoBox::encrypt_payload(request_payload, sym_key_, encrypted_payload)) { std::lock_guard<std::mutex> map_lock(rpc_map_mutex_); rpc_map_.erase(req_id); return {}; }
    { std::lock_guard<std::mutex> lock(kcp_mutex_); ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted_payload.data()), encrypted_payload.size()); ikcp_flush(kcp_cb_); }
    std::vector<uint8_t> ret;
    { std::unique_lock<std::mutex> lock(ctx->mtx); if (ctx->cv.wait_for(lock, std::chrono::seconds(30), [&ctx] { return ctx->done; })) { ret = std::move(ctx->response); } }
    { std::lock_guard<std::mutex> map_lock(rpc_map_mutex_); rpc_map_.erase(req_id); }
    return ret;
}

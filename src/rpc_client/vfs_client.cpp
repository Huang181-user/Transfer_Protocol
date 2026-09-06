// src/rpc_client/vfs_client.cpp
#include "rpc_client/vfs_client.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_packet.h"
#include "common/logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

using asio::ip::udp;

static uint32_t crc32_hash(const std::string& str) {
    uint32_t crc = 0xFFFFFFFF;
    for (char c : str) {
        crc ^= static_cast<uint32_t>(c);
        for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

VfsClient::VfsClient(const std::string& server_ip, uint16_t port, const std::string& sym_key, int mtu,
                     int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd, const std::string& hwid)
    : socket_(io_context_, udp::endpoint(udp::v4(), 0)), is_running_(false), 
      recv_buffer_(16777216), kcp_cb_(nullptr), sym_key_(sym_key), mtu_(mtu),
      nodelay_(nodelay), interval_(interval), resend_(resend), nc_(nc), snd_wnd_(snd_wnd), rcv_wnd_(rcv_wnd)
{
    client_id_ = crc32_hash(hwid);
    asio::ip::udp::resolver resolver(io_context_);
    server_endpoint_ = *resolver.resolve(udp::v4(), server_ip, std::to_string(port)).begin();
    try { socket_.set_option(asio::socket_base::receive_buffer_size(16777216)); socket_.set_option(asio::socket_base::send_buffer_size(16777216)); } catch(...) {}
}

VfsClient::~VfsClient() { stop(); }

bool VfsClient::start() {
    if (is_running_) return true;
    is_running_ = true;
    kcp_cb_ = ikcp_create(0x11223344, this);
    kcp_cb_->output = kcp_output_callback;
    
    ikcp_nodelay(kcp_cb_, nodelay_, interval_, resend_, nc_);
    int safe_mtu = (mtu_ > 100) ? (mtu_ - 56) : 1350; 
    ikcp_wndsize(kcp_cb_, snd_wnd_, rcv_wnd_); 
    kcp_cb_->stream = 0; ikcp_setmtu(kcp_cb_, safe_mtu); kcp_cb_->rx_minrto = 10;
    
    ZHI_LOG_INFO("[KCP-ENGINE] Mở đường hầm C++ KCP: NoDelay=" + std::to_string(nodelay_) + " MTU=" + std::to_string(safe_mtu));
    io_thread_ = std::thread(&VfsClient::receive_loop, this);
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    return true;
}

void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false;
    std::error_code ec; socket_.shutdown(asio::ip::udp::socket::shutdown_both, ec); socket_.close(ec);
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
    while (is_running_) {
        std::error_code ec; asio::ip::udp::endpoint sender;
        size_t bytes_recvd = socket_.receive_from(asio::buffer(recv_buffer_), sender, 0, ec);
        if (ec || bytes_recvd == 0) continue;

        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_input(kcp_cb_, reinterpret_cast<const char*>(recv_buffer_.data()), bytes_recvd);
        
        int len;
        while ((len = ikcp_peeksize(kcp_cb_)) > 0) {
            std::vector<uint8_t> encrypted_payload(len);
            if (ikcp_recv(kcp_cb_, reinterpret_cast<char*>(encrypted_payload.data()), len) < 0) break;

            std::vector<uint8_t> plaintext;
            if (CryptoBox::decrypt_payload(encrypted_payload, sym_key_, plaintext) && plaintext.size() >= sizeof(VfsPacketHeader)) {
                VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
                uint32_t req_id = (uint32_t)(hdr->session_id & 0xFFFFFFFF);
                
                std::lock_guard<std::mutex> rpc_lock(rpc_map_mutex_);
                auto it = rpc_map_.find(req_id);
                if (it != rpc_map_.end()) {
                    it->second->response = plaintext;
                    it->second->done = true;
                    it->second->cv.notify_all();
                }
            }
        }
    }
}

void VfsClient::kcp_update_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uint32_t clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (kcp_cb_) ikcp_update(kcp_cb_, clock);
    }
}

std::vector<uint8_t> VfsClient::send_rpc_sync(const std::vector<uint8_t>& payload, uint32_t req_id) {
    auto ctx = std::make_shared<RpcContext>();
    { std::lock_guard<std::mutex> lock(rpc_map_mutex_); rpc_map_[req_id] = ctx; }

    std::vector<uint8_t> encrypted;
    if (CryptoBox::encrypt_payload(payload, sym_key_, encrypted)) {
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        ikcp_flush(kcp_cb_);
    }

    std::unique_lock<std::mutex> wait_lock(ctx->mtx);
    if (ctx->cv.wait_for(wait_lock, std::chrono::seconds(60), [&]{ return ctx->done; })) {
        std::vector<uint8_t> res = ctx->response;
        std::lock_guard<std::mutex> clean_lock(rpc_map_mutex_); rpc_map_.erase(req_id);
        return res;
    }

    ZHI_LOG_ERR("[RPC-TIMEOUT] C++ KCP đứt gánh với ReqID: " + std::to_string(req_id));
    std::lock_guard<std::mutex> clean_lock(rpc_map_mutex_); rpc_map_.erase(req_id);
    return {};
}

void VfsClient::send_rpc_async(const std::vector<uint8_t>& payload, uint32_t req_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> encrypted;
    if (CryptoBox::encrypt_payload(payload, sym_key_, encrypted)) {
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        ikcp_flush(kcp_cb_);
    }
    auto enc_time = std::chrono::high_resolution_clock::now();
    auto encrypt_us = std::chrono::duration_cast<std::chrono::microseconds>(enc_time - start_time).count();
    ZHI_LOG_DEBUG("[KCP-PERF] [ASYNC] ReqID: " + std::to_string(req_id) + " | Payload: " + std::to_string(payload.size()) + "B | Encrypt: " + std::to_string(encrypt_us) + "us");
}
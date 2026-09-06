#include "rpc_client/vfs_client.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_packet.h"
#include "common/logger.h"
#include "system/sys_utils.hpp"
#include <iostream>

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
    : socket_(INVALID_SOCKET), is_running_(false), kcp_cb_(nullptr), sym_key_(sym_key), mtu_(mtu),
      nodelay_(nodelay), interval_(interval), resend_(resend), nc_(nc), snd_wnd_(snd_wnd), rcv_wnd_(rcv_wnd)
{
    client_id_ = crc32_hash(hwid);
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    u_long mode = 1; ioctlsocket(socket_, FIONBIO, &mode);
    int buf_sz = 16777216;
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (const char*)&buf_sz, sizeof(buf_sz));
    setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (const char*)&buf_sz, sizeof(buf_sz));

    memset(&server_endpoint_, 0, sizeof(server_endpoint_));
    server_endpoint_.sin_family = AF_INET;
    server_endpoint_.sin_port = htons(port);
    server_endpoint_.sin_addr.s_addr = inet_addr(SysUtils::resolve_host_ipv4(server_ip).c_str());
}

VfsClient::~VfsClient() { stop(); WSACleanup(); }

bool VfsClient::start() {
    if (is_running_) return true;
    is_running_ = true;
    kcp_cb_ = ikcp_create(0x11223344, this);
    kcp_cb_->output = kcp_output_callback;
    ikcp_nodelay(kcp_cb_, nodelay_, interval_, resend_, nc_);
    int safe_mtu = (mtu_ > 100) ? (mtu_ - 56) : 1350; 
    ikcp_wndsize(kcp_cb_, snd_wnd_, rcv_wnd_); 
    kcp_cb_->stream = 0; ikcp_setmtu(kcp_cb_, safe_mtu); kcp_cb_->rx_minrto = 10;
    
    ZHI_LOG_INFO("[KCP-ENGINE] Mo duong ham NATIVE WINSOCK2 KCP: MTU=" + std::to_string(safe_mtu));
    io_thread_ = std::thread(&VfsClient::receive_loop, this);
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    return true;
}

void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false;
    if (socket_ != INVALID_SOCKET) { closesocket(socket_); socket_ = INVALID_SOCKET; }
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (kcp_cb_) { ikcp_release(kcp_cb_); kcp_cb_ = nullptr; }
}

int VfsClient::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsClient* client = static_cast<VfsClient*>(user);
    if (client->socket_ != INVALID_SOCKET) {
        sendto(client->socket_, buf, len, 0, (struct sockaddr*)&client->server_endpoint_, sizeof(client->server_endpoint_));
    }
    return 0;
}

void VfsClient::receive_loop() {
    recv_buffer_.resize(65536);
    while (is_running_) {
        int addr_len = sizeof(server_endpoint_);
        int n = recvfrom(socket_, (char*)recv_buffer_.data(), recv_buffer_.size(), 0, (struct sockaddr*)&server_endpoint_, &addr_len);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(kcp_mutex_);
            ikcp_input(kcp_cb_, (const char*)recv_buffer_.data(), n);
            int kcp_len;
            while ((kcp_len = ikcp_peeksize(kcp_cb_)) > 0) {
                std::vector<uint8_t> encrypted_payload(kcp_len);
                ikcp_recv(kcp_cb_, (char*)encrypted_payload.data(), kcp_len);
                std::vector<uint8_t> plaintext;
                if (CryptoBox::decrypt_payload(encrypted_payload, sym_key_, plaintext) && plaintext.size() >= sizeof(VfsPacketHeader)) {
                    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
                    uint32_t req_id = (uint32_t)(hdr->session_id & 0xFFFFFFFF);
                    std::shared_ptr<RpcContext> ctx;
                            {
                                std::lock_guard<std::mutex> rpc_lock(rpc_map_mutex_);
                                auto it = rpc_map_.find(req_id);
                                if (it != rpc_map_.end()) ctx = it->second;
                            }
                            if (ctx) {
                                std::lock_guard<std::mutex> ctx_lock(ctx->mtx);
                                ctx->response = std::move(plaintext);
                                ctx->done = true;
                                ctx->cv.notify_all();
                            }
                }
            }
        } else { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
}

void VfsClient::kcp_update_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uint32_t clock = GetTickCount();
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (kcp_cb_) ikcp_update(kcp_cb_, clock);
    }
}

std::vector<uint8_t> VfsClient::send_rpc_sync(const std::vector<uint8_t>& payload, uint32_t req_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto ctx = std::make_shared<RpcContext>();
    { std::lock_guard<std::mutex> lock(rpc_map_mutex_); rpc_map_[req_id] = ctx; }

    std::vector<uint8_t> encrypted;
    if (CryptoBox::encrypt_payload(payload, sym_key_, encrypted)) {
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        ikcp_flush(kcp_cb_);
    }
    
    auto enc_time = std::chrono::high_resolution_clock::now();

    std::unique_lock<std::mutex> wait_lock(ctx->mtx);
    if (ctx->cv.wait_for(wait_lock, std::chrono::seconds(60), [&]{ return ctx->done; })) {
        std::vector<uint8_t> res = ctx->response;
        { std::lock_guard<std::mutex> clean_lock(rpc_map_mutex_); rpc_map_.erase(req_id); }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto encrypt_us = std::chrono::duration_cast<std::chrono::microseconds>(enc_time - start_time).count();
        auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - enc_time).count();
        
        // 🔥 IN DEBUG ĐO LƯỜNG TẦNG NETWORK & ENCRYPTION
        ZHI_LOG_DEBUG("[KCP-PERF] ReqID: " + std::to_string(req_id) + " | Payload: " + std::to_string(payload.size()) + "B | Encrypt: " + std::to_string(encrypt_us) + "us | Net RTT: " + std::to_string(rtt_ms) + "ms");
        
        return res;
    }

    ZHI_LOG_ERR("[RPC-TIMEOUT] C++ KCP dut ganh voi ReqID: " + std::to_string(req_id) + " sau 60s cho doi!");
    { std::lock_guard<std::mutex> clean_lock(rpc_map_mutex_); rpc_map_.erase(req_id); }
    return {};
}

// 🔥 HÀM BẮN TỈA KHÔNG CẦN CHỜ ĐỢI
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
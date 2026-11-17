#include "bridge/client_bridge.h"
#include "rpc_client/vfs_client.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_packet.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <string>

using asio::ip::udp;

extern "C" void zhiauth_swift_on_response(uint32_t reqId, const uint8_t* payload, uint32_t len);
extern "C" void zhiauth_swift_log(const char* msg);

static void logToSwift(const std::string& msg) {
    zhiauth_swift_log(msg.c_str());
}

static std::string getRealtimeLog() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
    std::tm bt{}; localtime_r(&time_t_now, &bt);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S") << "." << std::setfill('0') << std::setw(6) << micros;
    return oss.str();
}

// 🔥 FIX SỐNG CÒN Ở ĐÂY: Trả lại ID chuẩn khớp 100% với Server
static uint32_t IOS_CLIENT_ID = 0x11223344;

VfsClient::VfsClient(const std::string& server_ip, uint16_t port, const std::string& sym_key, int mtu,
                     int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd)
    : socket_(io_context_, udp::endpoint(udp::v4(), 0)), is_running_(false), 
      recv_buffer_(65536), kcp_cb_(nullptr), sym_key_(sym_key), mtu_(mtu),
      nodelay_(nodelay), interval_(interval), resend_(resend), nc_(nc), snd_wnd_(snd_wnd), rcv_wnd_(rcv_wnd)
{
    asio::ip::udp::resolver resolver(io_context_);
    server_endpoint_ = *resolver.resolve(udp::v4(), server_ip, std::to_string(port)).begin();
    try {
        socket_.set_option(asio::socket_base::receive_buffer_size(16777216));
        socket_.set_option(asio::socket_base::send_buffer_size(16777216));
    } catch(...) {}
}

VfsClient::~VfsClient() { stop(); }

bool VfsClient::start() {
    if (is_running_) return true;
    is_running_ = true;
    
    std::error_code ec;
    socket_.connect(server_endpoint_, ec);
    if (ec) {
        logToSwift("[" + getRealtimeLog() + "] ❌ [SYS-ERR] LỖI BIND SOCKET UDP: " + ec.message());
    } else {
        logToSwift("[" + getRealtimeLog() + "] 🟢 [SYS-NET] Mở Socket UDP trói IP: " + server_endpoint_.address().to_string() + ":" + std::to_string(server_endpoint_.port()));
    }

    kcp_cb_ = ikcp_create(IOS_CLIENT_ID, this);
    kcp_cb_->output = kcp_output_callback;
    
    ikcp_nodelay(kcp_cb_, nodelay_, interval_, resend_, nc_);
    int safe_mtu = (mtu_ > 100) ? (mtu_ - 56) : 1200; 
    ikcp_wndsize(kcp_cb_, snd_wnd_, rcv_wnd_); 
    kcp_cb_->stream = 0; ikcp_setmtu(kcp_cb_, safe_mtu); kcp_cb_->rx_minrto = 10;

    io_thread_ = std::thread([this]() { auto work = asio::make_work_guard(io_context_); receive_loop(); io_context_.run(); });
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    return true;
}

void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false; 
    
    std::error_code ec;
    socket_.cancel(ec); 
    socket_.close(ec);
    io_context_.stop();
    
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (kcp_cb_) { ikcp_release(kcp_cb_); kcp_cb_ = nullptr; }
}

int VfsClient::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsClient* client = static_cast<VfsClient*>(user);
    std::error_code ec;
    size_t sent = client->socket_.send(asio::buffer(buf, len), 0, ec);
    return 0;
}

void VfsClient::receive_loop() {
    socket_.async_receive(asio::buffer(recv_buffer_), 
        [this](std::error_code ec, std::size_t bytes_recvd) {
            if (!is_running_) return;
            if (ec) {
                if (ec != asio::error::would_block && ec != asio::error::try_again && ec != asio::error::operation_aborted) {
                    logToSwift("[" + getRealtimeLog() + "] ❌ [RX-ERR] Lỗi Socket UDP: " + ec.message());
                }
            } else if (bytes_recvd > 0) {
                std::lock_guard<std::mutex> lock(kcp_mutex_);
                int kcp_res = ikcp_input(kcp_cb_, reinterpret_cast<const char*>(recv_buffer_.data()), static_cast<long>(bytes_recvd));
                
                if (kcp_res < 0) {
                    size_t probe_size = (bytes_recvd >= 55) ? 55 : bytes_recvd;
                    std::vector<uint8_t> raw_payload(recv_buffer_.begin(), recv_buffer_.begin() + probe_size);
                    std::vector<uint8_t> plaintext;
                    
                    if (CryptoBox::decrypt_payload(raw_payload, sym_key_, plaintext) && plaintext.size() >= sizeof(VfsPacketHeader)) {
                        VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
                        if (hdr->opcode == VfsOpcode::OP_DEATH_WARNING) {
                            logToSwift("[" + getRealtimeLog() + "] 💥 Nhận Cảnh báo Tử thần (0xDE)! Tự văng App...");
                            uint32_t death_req = 999999;
                            zhiauth_swift_on_response(death_req, plaintext.data(), static_cast<uint32_t>(plaintext.size()));
                        } else if (hdr->opcode == VfsOpcode::OP_PING && hdr->magic == 0x5A484941) {
                            uint32_t req_id = static_cast<uint32_t>(hdr->session_id & 0xFFFFFFFF);
                            zhiauth_swift_on_response(req_id, plaintext.data(), static_cast<uint32_t>(plaintext.size()));
                        }
                    }
                } else {
                    int len;
                    while ((len = ikcp_peeksize(kcp_cb_)) > 0) {
                        std::vector<uint8_t> encrypted_payload(len);
                        ikcp_recv(kcp_cb_, reinterpret_cast<char*>(encrypted_payload.data()), len);
                        std::vector<uint8_t> plaintext;
                        if (CryptoBox::decrypt_payload(encrypted_payload, sym_key_, plaintext) && plaintext.size() >= sizeof(VfsPacketHeader)) {
                            VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plaintext.data());
                            
                            if (hdr->opcode == VfsOpcode::OP_DEATH_WARNING) {
                                logToSwift("[" + getRealtimeLog() + "] 💥 Nhận Cảnh báo Tử thần (0xDE)! Tự văng App...");
                                uint32_t death_req = 999999;
                                zhiauth_swift_on_response(death_req, plaintext.data(), static_cast<uint32_t>(plaintext.size()));
                                continue;
                            }

                            uint32_t req_id = static_cast<uint32_t>(hdr->session_id & 0xFFFFFFFF);
                            zhiauth_swift_on_response(req_id, plaintext.data(), static_cast<uint32_t>(plaintext.size()));
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
        uint32_t current_clock = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (kcp_cb_) ikcp_update(kcp_cb_, current_clock);
    }
}

void VfsClient::send_rpc_async(const std::vector<uint8_t>& request_payload) {
    if (request_payload.size() < sizeof(VfsPacketHeader)) return;
    
    std::vector<uint8_t> fixed_payload = request_payload;
    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(fixed_payload.data());
    
    // Ghi đề ID theo cấu trúc [ClientID 32 bit cao] [ReqID 32 bit thấp]
    uint32_t req_id = static_cast<uint32_t>(hdr->session_id & 0xFFFFFFFF);
    hdr->session_id = ((uint64_t)IOS_CLIENT_ID << 32) | req_id;
    
    bool is_ping = (hdr->opcode == VfsOpcode::OP_PING);

    std::vector<uint8_t> encrypted_payload;
    if (!CryptoBox::encrypt_payload(fixed_payload, sym_key_, encrypted_payload)) {
        logToSwift("[" + getRealtimeLog() + "] ❌ [TX-ERR] Ép giáp Libsodium thất bại!");
        return;
    }
    
    if (is_ping) {
        std::error_code ec;
        socket_.send(asio::buffer(encrypted_payload.data(), encrypted_payload.size()), 0, ec);
    } else {
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted_payload.data()), static_cast<int>(encrypted_payload.size()));
        ikcp_flush(kcp_cb_);
    }
}
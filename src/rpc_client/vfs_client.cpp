#include "bridge/client_bridge.h"
#include "rpc_client/vfs_client.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_packet.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <fstream>

using asio::ip::udp;

extern "C" void zhiauth_swift_on_response(uint32_t reqId, const uint8_t* payload, uint32_t len);

static std::string getRealtimeLog() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
    std::tm bt{}; localtime_r(&time_t_now, &bt);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(6) << micros;
    return oss.str();
}

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
        std::cout << "[" << getRealtimeLog() << "] UDP Connect Warning: " << ec.message() << std::endl;
    }

    kcp_cb_ = ikcp_create(0x11223344, this);
    kcp_cb_->output = kcp_output_callback;
    
    ikcp_nodelay(kcp_cb_, nodelay_, interval_, resend_, nc_);
    int safe_mtu = (mtu_ > 100) ? (mtu_ - 56) : 1200; 
    ikcp_wndsize(kcp_cb_, snd_wnd_, rcv_wnd_); 
    kcp_cb_->stream = 0; ikcp_setmtu(kcp_cb_, safe_mtu); kcp_cb_->rx_minrto = 10;

    std::cout << "[" << getRealtimeLog() << "] KCP Engine Operational | Safe MTU: " << safe_mtu << std::endl;

    io_thread_ = std::thread(&VfsClient::receive_loop, this);
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    return true;
}

void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false; socket_.close();
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (kcp_cb_) { ikcp_release(kcp_cb_); kcp_cb_ = nullptr; }
}

int VfsClient::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsClient* client = static_cast<VfsClient*>(user);
    std::error_code ec;
    client->socket_.send(asio::buffer(buf, len), 0, ec);
    return 0;
}

void VfsClient::receive_loop() {
    while (is_running_) {
        std::error_code ec;
        size_t bytes_recvd = socket_.receive(asio::buffer(recv_buffer_), 0, ec);
        
        if (ec || bytes_recvd == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue; 
        }

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
                    uint32_t req_id = (uint32_t)(hdr->session_id & 0xFFFFFFFF);
                    zhiauth_swift_on_response(req_id, plaintext.data(), plaintext.size());
                }
            }
        }
    }
}

void VfsClient::kcp_update_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        uint32_t current_clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (kcp_cb_) ikcp_update(kcp_cb_, current_clock);
    }
}

void VfsClient::send_rpc_async(const std::vector<uint8_t>& request_payload) {
    if (request_payload.size() < sizeof(VfsPacketHeader)) return;
    std::vector<uint8_t> encrypted_payload;
    if (!CryptoBox::encrypt_payload(request_payload, sym_key_, encrypted_payload)) return;
    {
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        ikcp_send(kcp_cb_, reinterpret_cast<const char*>(encrypted_payload.data()), encrypted_payload.size());
        ikcp_flush(kcp_cb_);
    }
}

extern "C" {
    void* zhiauth_create_vfs_client(const char* ip, int port, const char* sym_key, int mtu,
                                    int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) {
        return new VfsClient(ip, port, sym_key, mtu, nodelay, interval, resend, nc, snd_wnd, rcv_wnd);
    }

    bool zhiauth_start_vfs_client(void* client) {
        if (!client) return false;
        return static_cast<VfsClient*>(client)->start();
    }

    void zhiauth_stop_vfs_client(void* client) {
        if (client) static_cast<VfsClient*>(client)->stop();
    }

    void zhiauth_destroy_vfs_client(void* client) {
        if (client) delete static_cast<VfsClient*>(client);
    }

    void zhiauth_vfs_send_rpc(void* client, uint32_t req_id, uint8_t opcode, const char* path, 
                              uint64_t offset, uint32_t req_len, const uint8_t* payload, uint32_t payload_len) {
        if (!client) return;
        
        std::vector<uint8_t> req_data(sizeof(VfsPacketHeader) + payload_len);
        VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req_data.data());
        
        memset(hdr, 0, sizeof(VfsPacketHeader));
        hdr->session_id = req_id;
        hdr->opcode = opcode;
        hdr->offset = offset;
        hdr->length = req_len;
        if (path) {
            strncpy(hdr->path, path, sizeof(hdr->path) - 1);
        }
        
        if (payload && payload_len > 0) {
            memcpy(req_data.data() + sizeof(VfsPacketHeader), payload, payload_len);
        }
        
        static_cast<VfsClient*>(client)->send_rpc_async(req_data);
    }
}

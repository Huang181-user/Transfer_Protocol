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
#include <sys/sysinfo.h>

using asio::ip::udp;

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

static size_t getSystemMaxRmem() {
    std::ifstream file("/proc/sys/net/core/rmem_max");
    size_t rmem = 0;
    if (file >> rmem && rmem > 0) return rmem;
    return 1073741824;
}

static size_t calculateAbsoluteMaxSocketBuffer() {
    struct sysinfo info;
    size_t free_ram_buf = 1073741824;
    if (sysinfo(&info) == 0) { free_ram_buf = info.freeram * info.mem_unit * 0.80; }
    size_t sys_rmem = getSystemMaxRmem();
    size_t target = std::max(free_ram_buf, sys_rmem);
    return std::clamp(target, static_cast<size_t>(512 * 1024 * 1024), static_cast<size_t>(4096ULL * 1024 * 1024));
}

VfsClient::VfsClient(const std::string& server_ip, uint16_t port, const std::string& sym_key, int mtu)
    : socket_(io_context_, udp::endpoint(udp::v4(), 0)), is_running_(false), 
      recv_buffer_(4194304), kcp_cb_(nullptr), sym_key_(sym_key), mtu_(mtu)
{
    asio::ip::udp::resolver resolver(io_context_);
    server_endpoint_ = *resolver.resolve(udp::v4(), server_ip, std::to_string(port)).begin();
    size_t auto_buf_sz = calculateAbsoluteMaxSocketBuffer();
    try {
        socket_.set_option(asio::socket_base::receive_buffer_size(auto_buf_sz));
        socket_.set_option(asio::socket_base::send_buffer_size(auto_buf_sz));
    } catch(...) {}
}

VfsClient::~VfsClient() { stop(); }

bool VfsClient::start() {
    if (is_running_) return true;
    is_running_ = true;
    kcp_cb_ = ikcp_create(0x11223344, this);
    kcp_cb_->output = kcp_output_callback;
    
    ikcp_nodelay(kcp_cb_, 1, 1, 1, 1);
    int safe_mtu = (mtu_ > 100) ? (mtu_ - 56) : 1350; 
    
    ikcp_wndsize(kcp_cb_, 65535, 65535); 
    kcp_cb_->stream = 1; 
    
    ikcp_setmtu(kcp_cb_, safe_mtu); 
    kcp_cb_->rx_minrto = 2;

    std::cout << "[" << getRealtimeLog() << "] [SPEED_TUNER_CLIENT] KCP Engine Turbo Mode Initialized | MTU: " << safe_mtu << std::endl;

    io_thread_ = std::thread([this]() { receive_loop(); io_context_.run(); });
    timer_thread_ = std::thread(&VfsClient::kcp_update_loop, this);
    tuner_thread_ = std::thread(&VfsClient::kcp_adaptive_tuner_loop, this);
    return true;
}

void VfsClient::stop() {
    if (!is_running_) return;
    is_running_ = false; io_context_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (tuner_thread_.joinable()) tuner_thread_.join();
    if (kcp_cb_) { ikcp_release(kcp_cb_); kcp_cb_ = nullptr; }
}

int VfsClient::kcp_output_callback(const char* buf, int len, ikcpcb* kcp, void* user) {
    VfsClient* client = static_cast<VfsClient*>(user);
    std::error_code ec;
    client->socket_.send_to(asio::buffer(buf, len), client->server_endpoint_, 0, ec);
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
                        zhiauth_cgo_on_response(req_id, plaintext.data(), plaintext.size());
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
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (kcp_cb_) ikcp_update(kcp_cb_, current_clock);
    }
}

void VfsClient::kcp_adaptive_tuner_loop() {
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(kcp_mutex_);
        if (!kcp_cb_) continue;

        int current_rtt = kcp_cb_->rx_srtt;
        if (current_rtt <= 0) current_rtt = 1;

        // 🔥 THÁO XÍCH KCP: Ép Window bung nắp cực đại không giới hạn, RTO mượt mà!
        ikcp_nodelay(kcp_cb_, 1, 1, 2, 1);
        kcp_cb_->rx_minrto = std::clamp(current_rtt, 10, 100);
        ikcp_wndsize(kcp_cb_, 65535, 65535); 
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

#include "client_bridge.h"
#include "../rpc_client/ikcp.h"
#include "../rpc_client/crypto_box.h"
#include "../rpc_client/vfs_packet.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <atomic>

struct BridgeRpcContext {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> response_payload;
    bool is_done = false;
};

static SOCKET g_udp_sock = INVALID_SOCKET;
static struct sockaddr_in g_server_addr;
static std::string g_sym_key;
static ikcpcb* g_kcp = NULL;
static std::thread g_worker_thread;
static std::atomic<bool> g_is_running(false);
static std::mutex g_kcp_mutex;
static std::unordered_map<uint64_t, std::shared_ptr<BridgeRpcContext>> g_rpc_registry;
static std::mutex g_registry_mutex;

static void bridge_realtime_log(const char *fmt, ...) {
    SYSTEMTIME st; GetLocalTime(&st);
    printf("[%04d-%02d-%02d %02d:%02d:%02d.%03d] [C++BRIDGE] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args);
    printf("\n"); fflush(stdout);
}

static int udp_output_callback(const char *buf, int len, ikcpcb *kcp, void *user) {
    if (g_udp_sock != INVALID_SOCKET) { 
        return sendto(g_udp_sock, buf, len, 0, (struct sockaddr*)&g_server_addr, sizeof(g_server_addr)); 
    }
    return -1;
}

static void kcp_background_worker() {
    std::vector<char> udp_buf(65536);
    std::vector<char> kcp_buf(8 * 1024 * 1024);

    while (g_is_running) {
        uint32_t current_clock = GetTickCount();

        { 
            std::lock_guard<std::mutex> lock(g_kcp_mutex); 
            if (g_kcp) ikcp_update(g_kcp, current_clock); 
        }

        bool got_packet = false;
        int n;
        
        // Hút sạch bộ đệm UDP
        while ((n = recvfrom(g_udp_sock, udp_buf.data(), static_cast<int>(udp_buf.size()), 0, NULL, NULL)) > 0) {
            got_packet = true;
            std::lock_guard<std::mutex> lock(g_kcp_mutex);
            if (g_kcp) {
                ikcp_input(g_kcp, udp_buf.data(), n);
                int kcp_len;
                while ((kcp_len = ikcp_peeksize(g_kcp)) > 0) {
                    if (kcp_len > (int)kcp_buf.size()) kcp_buf.resize(kcp_len);
                    int read_bytes = ikcp_recv(g_kcp, kcp_buf.data(), kcp_len);
                    if (read_bytes <= 0) break;
                    
                    std::vector<uint8_t> enc_resp(kcp_buf.begin(), kcp_buf.begin() + read_bytes);
                    std::vector<uint8_t> plain_resp;
                    
                    if (CryptoBox::decrypt_payload(enc_resp, g_sym_key, plain_resp)) {
                        if (plain_resp.size() >= sizeof(VfsPacketHeader)) {
                            VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(plain_resp.data());
                            uint64_t req_id = hdr->session_id;

                            std::shared_ptr<BridgeRpcContext> ctx;
                            { 
                                std::lock_guard<std::mutex> reg_lock(g_registry_mutex); 
                                auto it = g_rpc_registry.find(req_id); 
                                if (it != g_rpc_registry.end()) ctx = it->second; 
                            }

                            if (ctx) {
                                std::lock_guard<std::mutex> ctx_lock(ctx->mtx);
                                ctx->response_payload = std::move(plain_resp);
                                ctx->is_done = true;
                                ctx->cv.notify_one();
                            }
                        }
                    }
                }
                // 🔥 ÉP BẮN ACK VỀ SERVER NGAY LẬP TỨC
                ikcp_flush(g_kcp);
            }
        }

        if (!got_packet) { 
            std::this_thread::sleep_for(std::chrono::microseconds(200)); 
        }
    }
}

extern "C" int zhiauth_client_init(const char* ip, int port, const char* sym_key, int mtu, uint32_t conv,
                                  int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) {
    if (g_is_running) return 0;
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_sock == INVALID_SOCKET) return -1;
    
    u_long mode = 1; ioctlsocket(g_udp_sock, FIONBIO, &mode);
    int win_buf_sz = 128 * 1024 * 1024;
    setsockopt(g_udp_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&win_buf_sz, sizeof(win_buf_sz));
    setsockopt(g_udp_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&win_buf_sz, sizeof(win_buf_sz));

    memset(&g_server_addr, 0, sizeof(g_server_addr));
    g_server_addr.sin_family = AF_INET; g_server_addr.sin_port = htons(port); g_server_addr.sin_addr.s_addr = inet_addr(ip);

    g_sym_key = sym_key;
    if (!CryptoBox::initialize()) return -3;

    g_kcp = ikcp_create(conv, NULL);
    g_kcp->output = udp_output_callback;
    ikcp_setmtu(g_kcp, mtu);
    ikcp_wndsize(g_kcp, snd_wnd, rcv_wnd);
    ikcp_nodelay(g_kcp, nodelay, interval, resend, nc);
    g_kcp->rx_minrto = 10; 
    g_kcp->dead_link = 200;

    g_is_running = true;
    g_worker_thread = std::thread(kcp_background_worker);
    bridge_realtime_log("🚀 HYPER KCP CLIENT ENGINE STARTED (CONV: %u, WND: %d/%d, NODELAY: %d/%d/%d/%d)", 
                        conv, snd_wnd, rcv_wnd, nodelay, interval, resend, nc);
    return 0;
}

extern "C" void zhiauth_client_shutdown() {
    bridge_realtime_log("🛑 Đang đóng gói C++ Engine...");
    g_is_running = false;
    if (g_worker_thread.joinable()) g_worker_thread.join();
    std::lock_guard<std::mutex> lock(g_kcp_mutex);
    if (g_kcp) { ikcp_release(g_kcp); g_kcp = NULL; }
    if (g_udp_sock != INVALID_SOCKET) closesocket(g_udp_sock);
    WSACleanup();
    bridge_realtime_log("✅ KCP C++ Engine Shutdown Hoàn Tất.");
}

extern "C" uint8_t* zhiauth_send_vfs_command(const uint8_t* payload, size_t len, size_t* out_len) {
    try {
        if (!g_is_running || !g_kcp) { *out_len = 0; return NULL; }
        if (len < sizeof(VfsPacketHeader)) { *out_len = 0; return NULL; }
        const VfsPacketHeader* hdr = reinterpret_cast<const VfsPacketHeader*>(payload);
        uint64_t req_id = hdr->session_id;

        auto ctx = std::make_shared<BridgeRpcContext>();
        { std::lock_guard<std::mutex> reg_lock(g_registry_mutex); g_rpc_registry[req_id] = ctx; }

        std::vector<uint8_t> plain_req(payload, payload + len);
        std::vector<uint8_t> enc_req;
        if (!CryptoBox::encrypt_payload(plain_req, g_sym_key, enc_req)) {
            std::lock_guard<std::mutex> reg_lock(g_registry_mutex);
            g_rpc_registry.erase(req_id); *out_len = 0; return NULL;
        }

        {
            std::lock_guard<std::mutex> lock(g_kcp_mutex);
            ikcp_send(g_kcp, (const char*)enc_req.data(), enc_req.size());
            ikcp_flush(g_kcp);
        }

        uint8_t* out_buf = NULL;
        {
            std::unique_lock<std::mutex> ctx_lock(ctx->mtx);
            if (ctx->cv.wait_for(ctx_lock, std::chrono::seconds(15), [&] { return ctx->is_done; })) {
                if (!ctx->response_payload.empty()) {
                    *out_len = ctx->response_payload.size();
                    out_buf = (uint8_t*)malloc(ctx->response_payload.size());
                    if (out_buf) memcpy(out_buf, ctx->response_payload.data(), ctx->response_payload.size());
                }
            } else {
                bridge_realtime_log("❌ [KCP-TIMEOUT] Hủy giao dịch ReqID: %llu do Server im lặng quá 15s!", req_id);
            }
        }

        { std::lock_guard<std::mutex> reg_lock(g_registry_mutex); g_rpc_registry.erase(req_id); }
        return out_buf;
    } catch (...) { *out_len = 0; return NULL; }
}
#include <jni.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <android/log.h>
#include "ikcp.h"
#include "crypto_box.h"
#include "rpc_quic/msquic_client.h"
#include "system/sys_utils.hpp"
#include "rpc_client/vfs_packet.h"

// 🚀 HỆ THỐNG LOG THỜI GIAN THỰC ĐẾN TỪNG MILISECOND
static void real_log(int prio, const char* fmt, ...) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    char buf[4096]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    __android_log_print(prio, "HUANG_C++_CORE", "[%lld ms] %s", (long long)ms, buf);
}
#define LOGI(...) real_log(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGE(...) real_log(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGW(...) real_log(ANDROID_LOG_WARN, __VA_ARGS__)

static int g_socket_fd = -1;
static ikcpcb* g_kcp = nullptr;
static std::string g_master_key;
static struct sockaddr_in g_server_addr;

static std::thread g_recv_thread, g_update_thread;
std::atomic<bool> g_running{false};
static std::atomic<uint32_t> g_req_id{0};
static uint32_t g_client_id = 0;
static std::mutex g_kcp_mutex;

// 🔥 XÓA CHỮ STATIC ĐỂ LUỒNG QUIC CÓ THỂ XÀI KÉ KHO PROMISE NÀY
struct RequestWait { std::vector<uint8_t> data; bool done = false; bool success = true; std::condition_variable cv; };
std::map<uint64_t, std::shared_ptr<RequestWait>> g_pending_requests;
std::mutex g_promise_mutex;

static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    if (g_socket_fd >= 0) {
        ssize_t sent = sendto(g_socket_fd, buf, len, 0, (struct sockaddr*)&g_server_addr, sizeof(g_server_addr));
        if (sent < 0) {
            LOGE("❌ [UDP-OUT] LỖI GỬI MẠNG: %s (errno=%d)", strerror(errno), errno);
        }
    }
    return 0;
}

static void kcp_update_loop() {
    auto last_hb = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_hb).count() >= 10) {
            last_hb = now;
            std::lock_guard<std::mutex> lock(g_kcp_mutex);
            if (g_kcp && g_running) {
                std::vector<uint8_t> pkt(27, 0); uint32_t magic = 0x5A484941; memcpy(&pkt[0], &magic, 4); pkt[4] = 0x00;
                std::vector<uint8_t> cipher;
                if (CryptoBox::encrypt_payload(pkt, g_master_key, cipher)) {
                    ikcp_send(g_kcp, (const char*)cipher.data(), cipher.size());
                    LOGI("💓 [KCP-PING] Đã bơm nhịp tim Ping giữ mạng!");
                }
            }
        }
        std::lock_guard<std::mutex> lock(g_kcp_mutex);
        if (g_kcp && g_running) {
            uint32_t current_clock = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF;
            ikcp_update(g_kcp, current_clock);
        }
    }
}

static void kcp_recv_loop() {
    uint8_t buf[65535]; std::vector<uint8_t> kcp_payload(6 * 1024 * 1024);
    LOGI("🎧 [KCP-RECV] Bắt đầu luồng lắng nghe UDP trên Socket fd = %d...", g_socket_fd);
    while (g_running) {
        struct timeval tv{0, 100000}; setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        struct sockaddr_in src; socklen_t len = sizeof(src);
        int n = recvfrom(g_socket_fd, buf, sizeof(buf), 0, (struct sockaddr*)&src, &len);

        if (n > 0) {
            std::lock_guard<std::mutex> lock(g_kcp_mutex);
            if (g_kcp && g_running) {
                int ret = ikcp_input(g_kcp, (const char*)buf, n);
                while (true) {
                    int p_size = ikcp_peeksize(g_kcp);
                    if (p_size < 0) break;
                    if (p_size > kcp_payload.size()) kcp_payload.resize(p_size + 1024 * 1024);
                    int r_len = ikcp_recv(g_kcp, (char*)kcp_payload.data(), kcp_payload.size());
                    if (r_len < 0) break;

                    std::vector<uint8_t> cipher(kcp_payload.data(), kcp_payload.data() + r_len), plain;
                    if (CryptoBox::decrypt_payload(cipher, g_master_key, plain)) {
                        if (plain.size() >= 27) {
                            uint64_t sess_id = 0; memcpy(&sess_id, &plain[5], 8);
                            uint8_t opcode = plain[4];
                            if (sess_id == 0) continue;

                            std::lock_guard<std::mutex> plock(g_promise_mutex);
                            auto it = g_pending_requests.find(sess_id);
                            if (it != g_pending_requests.end()) {
                                if (opcode == 0xFF) {
                                    LOGE("💀 [KCP-RECV] Server báo lỗi OP_ERROR (0xFF) cho SessionID %llu", sess_id);
                                    it->second->success = false;
                                } else {
                                    it->second->success = true;
                                    it->second->data = std::vector<uint8_t>(plain.begin() + 27, plain.end());
                                }
                                it->second->done = true;
                                it->second->cv.notify_all();
                            }
                        }
                    }
                }
            }
        }
    }
    LOGI("🛑 [KCP-RECV] Luồng lắng nghe UDP đã đóng hoàn toàn.");
}

std::vector<uint8_t> build_vfs_packet(uint8_t opcode, uint64_t session_id, uint64_t offset, uint32_t data_len, const char* c_path, jbyte* c_data) {
    uint16_t path_len = strlen(c_path);
    std::vector<uint8_t> pkt; pkt.reserve(27 + path_len + data_len); uint32_t magic = 0x5A484941;
    pkt.insert(pkt.end(), (uint8_t*)&magic, ((uint8_t*)&magic) + 4);
    pkt.push_back(opcode);
    pkt.insert(pkt.end(), (uint8_t*)&session_id, ((uint8_t*)&session_id) + 8);
    pkt.insert(pkt.end(), (uint8_t*)&offset, ((uint8_t*)&offset) + 8);
    pkt.insert(pkt.end(), (uint8_t*)&data_len, ((uint8_t*)&data_len) + 4);
    pkt.insert(pkt.end(), (uint8_t*)&path_len, ((uint8_t*)&path_len) + 2);
    pkt.insert(pkt.end(), c_path, c_path + path_len);
    if (c_data && data_len > 0 && opcode != 0x03) pkt.insert(pkt.end(), c_data, c_data + data_len);
    return pkt;
}

extern "C" JNIEXPORT jint JNICALL Java_com_example_transfer_1server_ZhiAuthNative_discoverMtu(JNIEnv *env, jobject thiz, jstring ip) {
    const char* c_ip = env->GetStringUTFChars(ip, 0); int mtu = SysUtils::discover_best_huang_mtu(c_ip); env->ReleaseStringUTFChars(ip, c_ip); return mtu;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_example_transfer_1server_ZhiAuthNative_getDeviceIps(JNIEnv *env, jobject thiz) {
    std::string lan = "NONE", ts = "NONE"; SysUtils::auto_detect_ips(lan, ts);
    if (lan.empty()) lan = "NONE"; if (ts.empty()) ts = "NONE";
    std::string res = lan + "|" + ts; return env->NewStringUTF(res.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_example_transfer_1server_ZhiAuthNative_initMsQuic(JNIEnv *env, jobject thiz, jstring ip, jint auth_port, jint data_port) {
    const char* c_ip = env->GetStringUTFChars(ip, 0); bool res = MsQuicClient::initialize(c_ip, auth_port, data_port); env->ReleaseStringUTFChars(ip, c_ip); return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_example_transfer_1server_ZhiAuthNative_authMsQuic(JNIEnv *env, jobject thiz, jstring payload) {
    const char* c_payload = env->GetStringUTFChars(payload, 0); std::string res = MsQuicClient::auth_sync(c_payload); env->ReleaseStringUTFChars(payload, c_payload); return env->NewStringUTF(res.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_example_transfer_1server_ZhiAuthNative_shutdownQuic(JNIEnv *env, jobject thiz) { MsQuicClient::shutdown(); }

extern "C" JNIEXPORT void JNICALL Java_com_example_transfer_1server_ZhiAuthNative_shutdownKcp(JNIEnv *env, jobject thiz);

extern "C" JNIEXPORT jboolean JNICALL Java_com_example_transfer_1server_ZhiAuthNative_initKcp(JNIEnv *env, jobject thiz, jstring serverIp, jint port, jstring masterKey, jint mtu, jint nodelay, jint interval, jint resend, jint nc, jint sndWnd, jint rcvWnd) {
    LOGI("🚀 [KCP-INIT] YÊU CẦU NỔ MÁY C++ KCP ENGINE...");
    if (g_running) Java_com_example_transfer_1server_ZhiAuthNative_shutdownKcp(env, thiz);
    g_client_id = (uint32_t)(std::chrono::system_clock::now().time_since_epoch().count() & 0xFFFFFFFF) ?: 1;

    const char *ip = env->GetStringUTFChars(serverIp, 0);
    const char *c_mk = env->GetStringUTFChars(masterKey, 0);
    g_master_key = std::string(c_mk);

    CryptoBox::initialize();
    g_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int rcv_buf = 16777216; setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVBUF, &rcv_buf, sizeof(rcv_buf));
    memset(&g_server_addr, 0, sizeof(g_server_addr)); g_server_addr.sin_family = AF_INET; g_server_addr.sin_port = htons(port); inet_pton(AF_INET, ip, &g_server_addr.sin_addr);
    g_kcp = ikcp_create(0x11223344, nullptr); g_kcp->output = udp_output;

    // 🔥 ĐÃ VÁ LỖI MTU Ở ĐÂY
    int safe_mtu = (mtu > 100) ? (mtu - 56) : 1350;
    ikcp_nodelay(g_kcp, nodelay, interval, resend, nc); ikcp_wndsize(g_kcp, sndWnd, rcvWnd); ikcp_setmtu(g_kcp, safe_mtu);
    g_running = true; g_recv_thread = std::thread(kcp_recv_loop); g_update_thread = std::thread(kcp_update_loop);
    LOGI("🔥 [KCP-INIT] ĐÃ LÊN NÒNG! Đích: %s:%d | MTU: %d", ip, port, safe_mtu);

    env->ReleaseStringUTFChars(serverIp, ip);
    env->ReleaseStringUTFChars(masterKey, c_mk);
    return JNI_TRUE;
}

// Tìm hàm Java_com_example_transfer_1server_ZhiAuthNative_sendRawKcp và chép đè:
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_example_transfer_1server_ZhiAuthNative_sendRawKcp(JNIEnv *env, jobject thiz, jbyte opcode, jstring path, jlong offset, jint reqLen, jbyteArray data) {
    if (!g_running) return nullptr;
    const char *c_path = env->GetStringUTFChars(path, 0); jbyte* c_data = data ? env->GetByteArrayElements(data, 0) : nullptr;
    uint32_t data_len = data ? env->GetArrayLength(data) : (opcode == 0x03 ? reqLen : 0);
    uint64_t sid = ((uint64_t)g_client_id << 32) | (++g_req_id);

    std::vector<uint8_t> pkt = build_vfs_packet(opcode, sid, offset, data_len, c_path, c_data);
    env->ReleaseStringUTFChars(path, c_path); if (data) env->ReleaseByteArrayElements(data, c_data, JNI_ABORT);

    std::vector<uint8_t> cipher;
    if (!CryptoBox::encrypt_payload(pkt, g_master_key, cipher)) return nullptr;

    auto waiter = std::make_shared<RequestWait>();
    { std::lock_guard<std::mutex> lock(g_promise_mutex); g_pending_requests[sid] = waiter; }

    {
        std::lock_guard<std::mutex> lock(g_kcp_mutex);
        if (g_kcp) {
            ikcp_send(g_kcp, (const char*)cipher.data(), cipher.size());
            ikcp_flush(g_kcp);
        }
    }

    // 🔥 NẾU LÀ OP_WRITE (0x04) THÌ BỎ QUA VIỆC CHỜ VÀ TRẢ VỀ FAKE SUCCESS LUÔN
    if (opcode == 0x04) {
        g_pending_requests.erase(sid);
        return env->NewByteArray(0);
    }

    std::unique_lock<std::mutex> wait_lock(g_promise_mutex);
    if (waiter->cv.wait_for(wait_lock, std::chrono::seconds(5), [&]{ return waiter->done; })) {
        g_pending_requests.erase(sid);
        if (!waiter->success) return nullptr;
        jbyteArray ret = env->NewByteArray(waiter->data.size());
        if (!waiter->data.empty()) env->SetByteArrayRegion(ret, 0, waiter->data.size(), (jbyte*)waiter->data.data());
        return ret;
    } else {
        LOGE("⏰ [KCP-SEND] TIMEOUT 5s! Không thấy Server trả lời SessionID %llu", sid);
        g_pending_requests.erase(sid);
        return nullptr;
    }
}

// 🔥 HÀM ĐẶC TRỊ CHO QUIC VFS ĐỂ TRÁNH DEADLOCK (CŨNG DÙNG CHUNG KHO PROMISE CỦA KCP)
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_example_transfer_1server_ZhiAuthNative_sendRawQuic(JNIEnv *env, jobject thiz, jbyte opcode, jstring path, jlong offset, jint reqLen, jbyteArray data) {
    if (!g_running) return nullptr;
    const char *c_path = env->GetStringUTFChars(path, 0);
    jbyte* c_data = data ? env->GetByteArrayElements(data, 0) : nullptr;
    uint32_t data_len = data ? env->GetArrayLength(data) : (opcode == 0x03 ? reqLen : 0);
    uint64_t sid = ((uint64_t)g_client_id << 32) | (++g_req_id);

    std::vector<uint8_t> pkt = build_vfs_packet(opcode, sid, offset, data_len, c_path, c_data);
    env->ReleaseStringUTFChars(path, c_path);
    if (data) env->ReleaseByteArrayElements(data, c_data, JNI_ABORT);

    auto waiter = std::make_shared<RequestWait>();
    { std::lock_guard<std::mutex> lock(g_promise_mutex); g_pending_requests[sid] = waiter; }

    if (!MsQuicClient::send_vfs_async(pkt, sid)) {
        LOGE("💀 [QUIC-SEND] MsQuic bắn gói tin thất bại!");
        g_pending_requests.erase(sid);
        return nullptr;
    }

    // 🔥 NẾU LÀ OP_WRITE (0x04) THÌ BỎ QUA VIỆC CHỜ VÀ TRẢ VỀ FAKE SUCCESS LUÔN
    if (opcode == 0x04) {
        g_pending_requests.erase(sid);
        return env->NewByteArray(0);
    }

    std::unique_lock<std::mutex> wait_lock(g_promise_mutex);
    if (waiter->cv.wait_for(wait_lock, std::chrono::seconds(5), [&]{ return waiter->done; })) {
        g_pending_requests.erase(sid);
        if (!waiter->success) return nullptr;
        jbyteArray ret = env->NewByteArray(waiter->data.size());
        if (!waiter->data.empty()) env->SetByteArrayRegion(ret, 0, waiter->data.size(), (jbyte*)waiter->data.data());
        return ret;
    } else {
        LOGE("⏰ [QUIC-SEND] TIMEOUT 5s! Không thấy Server trả lời SessionID %llu", sid);
        g_pending_requests.erase(sid);
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_example_transfer_1server_ZhiAuthNative_shutdownKcp(JNIEnv *env, jobject thiz) {
    LOGI("🛑 [KCP-SHUTDOWN] Đang dập tắt Engine...");
    g_running = false;
    { std::lock_guard<std::mutex> lock(g_promise_mutex); for (auto& p : g_pending_requests) { p.second->done = true; p.second->cv.notify_all(); } }
    if (g_recv_thread.joinable()) g_recv_thread.join(); if (g_update_thread.joinable()) g_update_thread.join();
    std::lock_guard<std::mutex> lock(g_kcp_mutex); if (g_kcp) { ikcp_release(g_kcp); g_kcp = nullptr; }
    if (g_socket_fd >= 0) { close(g_socket_fd); g_socket_fd = -1; }
}

extern "C" JNIEXPORT void JNICALL Java_com_example_transfer_1server_ZhiAuthNative_reconnectSocket(JNIEnv *env, jobject thiz) {
    if (!g_running) return;
    std::lock_guard<std::mutex> lock(g_kcp_mutex); if (g_socket_fd >= 0) { close(g_socket_fd); g_socket_fd = -1; }
    g_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket_fd >= 0) { int s = 16777216; setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVBUF, &s, sizeof(s)); }
    std::lock_guard<std::mutex> plock(g_promise_mutex); for (auto& p : g_pending_requests) { p.second->data.clear(); p.second->done = true; p.second->success = false; p.second->cv.notify_all(); }
    g_pending_requests.clear();
}
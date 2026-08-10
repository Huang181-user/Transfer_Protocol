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
#include <stdarg.h>
#include "ikcp.h"
#include "crypto_box.h"

// =====================================================================
// 🔥 HỆ THỐNG LOG KÈM REALTIME ĐỂ SIÊU GỠ LỖI (HUANG_C++_CORE)
// =====================================================================
static void real_log(int prio, const char* fmt, ...) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    __android_log_print(prio, "HUANG_C++_CORE", "[REALTIME: %lld ms] %s", (long long)ms, buf);
}
#define LOGI(...) real_log(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGE(...) real_log(ANDROID_LOG_ERROR, __VA_ARGS__)

// =====================================================================
// 🔥 CHE GIẤU ĐƯỜNG DẪN C++ KHÔNG DÙNG REGEX
// =====================================================================
static std::string mask_sensitive_path(const std::string& path) {
    if (path.empty()) return path;

    // Tìm vị trí dấu '/' cuối cùng
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == 0) {
        return path; // Trả về gốc nếu ko phải chuỗi đường dẫn lồng nhau
    }

    // Cắt lấy tên file cuối cùng để dễ debug
    std::string filename = path.substr(last_slash + 1);

    // Nối với lớp mặt nạ
    return "/***/***/" + filename;
}

static int g_socket_fd = -1;
static ikcpcb* g_kcp = nullptr;
static std::string g_master_key;
static struct sockaddr_in g_server_addr;

static std::thread g_recv_thread;
static std::thread g_update_thread;
static std::atomic<bool> g_running{false};
static std::atomic<uint32_t> g_req_id{0};
static uint32_t g_client_id = 0;

static std::mutex g_kcp_mutex;
static std::mutex g_promise_mutex;

struct RequestWait {
    std::vector<uint8_t> data;
    bool done = false;
    std::condition_variable cv;
};
static std::map<uint64_t, std::shared_ptr<RequestWait>> g_pending_requests;

static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    if (g_socket_fd >= 0) {
        // Đã connect nên vã lệnh send thẳng tay, cực kỳ nhẹ và tránh lạc IP
        send(g_socket_fd, buf, len, 0);
    }
    return 0;
}

static void kcp_update_loop() {
    auto last_heartbeat = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto now = std::chrono::steady_clock::now();
        bool send_ping = false;
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() >= 10) {
            send_ping = true;
            last_heartbeat = now;
        }

        std::lock_guard<std::mutex> lock(g_kcp_mutex);
        if (g_kcp && g_running) {
            if (send_ping) {
                std::vector<uint8_t> vfs_packet;
                uint32_t magic = 0x5A484941;
                uint64_t reqId = 0;
                uint64_t offset = 0;
                uint32_t data_len = 0;
                uint16_t path_len = 0;

                vfs_packet.insert(vfs_packet.end(), (uint8_t*)&magic, ((uint8_t*)&magic) + 4);
                vfs_packet.push_back(0x00); // OP_PING (0x00)
                vfs_packet.insert(vfs_packet.end(), (uint8_t*)&reqId, ((uint8_t*)&reqId) + 8);
                vfs_packet.insert(vfs_packet.end(), (uint8_t*)&offset, ((uint8_t*)&offset) + 8);
                vfs_packet.insert(vfs_packet.end(), (uint8_t*)&data_len, ((uint8_t*)&data_len) + 4);
                vfs_packet.insert(vfs_packet.end(), (uint8_t*)&path_len, ((uint8_t*)&path_len) + 2);

                std::vector<uint8_t> ciphertext;
                if (CryptoBox::encrypt_payload(vfs_packet, g_master_key, ciphertext)) {
                    ikcp_send(g_kcp, (const char*)ciphertext.data(), ciphertext.size());
                    LOGI("💓 [Heartbeat] Đã bơm máu 10s cho đường truyền KCP!");
                }
            }

            uint32_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() & 0xFFFFFFFF;
            ikcp_update(g_kcp, current_ms);
        }
    }
}

static void kcp_recv_loop() {
    uint8_t udp_buffer[65535];
    std::vector<uint8_t> kcp_payload(6 * 1024 * 1024); // Đệm lớn 6MB cho Window Size 4096

    while (g_running) {
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 100000;
        setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        int n = recvfrom(g_socket_fd, udp_buffer, sizeof(udp_buffer), 0, (struct sockaddr*)&src_addr, &src_len);

        if (n > 0) {
            std::lock_guard<std::mutex> lock(g_kcp_mutex);
            if (g_kcp && g_running) {
                ikcp_input(g_kcp, (const char*)udp_buffer, n);

                while (true) {
                    int peek_size = ikcp_peeksize(g_kcp);
                    if (peek_size < 0) break;

                    if (peek_size > kcp_payload.size()) {
                        LOGE("⚠️ Kích thước gói KCP quá lớn (%d bytes)! Tự động nới túi...", peek_size);
                        kcp_payload.resize(peek_size + 1024 * 1024);
                    }

                    int read_len = ikcp_recv(g_kcp, (char*)kcp_payload.data(), kcp_payload.size());
                    if (read_len < 0) {
                        LOGE("❌ ikcp_recv LỖI! Mã lỗi: %d | Peek_size dự kiến: %d", read_len, peek_size);
                        break;
                    }

                    std::vector<uint8_t> ciphertext(kcp_payload.data(), kcp_payload.data() + read_len);
                    std::vector<uint8_t> plaintext;

                    if (CryptoBox::decrypt_payload(ciphertext, g_master_key, plaintext)) {
                        if (plaintext.size() >= 27 && plaintext[4] != 0xFF) {
                            uint64_t session_id = 0;
                            memcpy(&session_id, &plaintext[5], 8);

                            if (session_id == 0) continue;

                            std::lock_guard<std::mutex> plock(g_promise_mutex);
                            auto it = g_pending_requests.find(session_id);
                            if (it != g_pending_requests.end()) {
                                it->second->data = std::vector<uint8_t>(plaintext.begin() + 27, plaintext.end());
                                it->second->done = true;
                                it->second->cv.notify_one();
                            } else {
                                LOGE("⚠️ Có data về ID %llu nhưng Không có ai chờ nhận hàng!", session_id);
                            }
                        } else if (plaintext.size() >= 27 && plaintext[4] == 0xFF) {
                            LOGE("❌ Server trả về mã lỗi 0xFF (Lỗi I/O hoặc File bị khóa)!");
                        } else {
                            LOGE("❌ Cấu trúc VFS Server gửi về dị thường! Size: %zu", plaintext.size());
                        }
                    } else {
                        LOGE("❌ Libsodium giải mã thất bại!");
                    }
                }
            }
        }
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_transfer_1server_KcpNative_initKcp(
        JNIEnv *env, jobject thiz,
        jstring serverIp, jint port, jstring masterKey, jint mtu,
        jint nodelay, jint interval, jint resend, jint nc, jint sndWnd, jint rcvWnd
) {
    if (g_running) return JNI_TRUE;

    if (g_client_id == 0) {
        srand(time(NULL));
        g_client_id = (uint32_t)(std::chrono::system_clock::now().time_since_epoch().count() & 0xFFFFFFFF);
        if (g_client_id == 0) g_client_id = 1;
    }

    const char *ip = env->GetStringUTFChars(serverIp, 0);
    const char *mk = env->GetStringUTFChars(masterKey, 0);
    g_master_key = mk;
    CryptoBox::initialize();

    g_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    int rcv_buf_size = 16777216;
    setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, sizeof(rcv_buf_size));

    memset(&g_server_addr, 0, sizeof(g_server_addr));
    g_server_addr.sin_family = AF_INET;
    g_server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &g_server_addr.sin_addr);

    if (connect(g_socket_fd, (struct sockaddr*)&g_server_addr, sizeof(g_server_addr)) < 0) {
        LOGE("❌ Lỗi ép kết nối Socket UDP! Lệch đường ray VPN!");
        return JNI_FALSE;
    }

    g_kcp = ikcp_create(0x11223344, nullptr);
    g_kcp->output = udp_output;

    ikcp_nodelay(g_kcp, nodelay, interval, resend, nc);
    ikcp_wndsize(g_kcp, sndWnd, rcvWnd);
    ikcp_setmtu(g_kcp, mtu - 107);

    g_running = true;
    g_recv_thread = std::thread(kcp_recv_loop);
    g_update_thread = std::thread(kcp_update_loop);

    LOGI("🚀 C++ NDK ENGINE KÍCH HOẠT THÀNH CÔNG! ĐÃ CONNECT SOCKET CHỐNG RỚT GÓI.");
    LOGI("📊 [KCP TUNING DYNAMIC] NoDelay=%d, Interval=%dms, Resend=%d, NC=%d, SND_WND=%d, RCV_WND=%d | MTU=%d",
         nodelay, interval, resend, nc, sndWnd, rcvWnd, mtu);

    env->ReleaseStringUTFChars(serverIp, ip);
    env->ReleaseStringUTFChars(masterKey, mk);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_example_transfer_1server_KcpNative_sendRawKcp(JNIEnv *env, jobject thiz, jbyte opcode, jstring path, jlong offset, jint reqLen, jbyteArray data) {
    if (!g_running) return nullptr;

    const char *c_path = env->GetStringUTFChars(path, 0);
    uint16_t path_len = strlen(c_path);
    uint32_t data_len = 0;
    jbyte* c_data = nullptr;

    if (data != nullptr) {
        data_len = env->GetArrayLength(data);
        c_data = env->GetByteArrayElements(data, 0);
    }
    if (opcode == 0x03) data_len = reqLen;

    uint32_t current_req = ++g_req_id;
    uint64_t session_id = ((uint64_t)g_client_id << 32) | current_req;

    // 🔥 GỌI HÀM LÀM MỜ ĐƯỜNG DẪN TRƯỚC KHI IN RA LOGCAT
    std::string masked_path = mask_sensitive_path(c_path);
    LOGI("📤 [SEND] Opcode: 0x%02X | Path: %s | SessionID: %llu | Size: %d", opcode, masked_path.c_str(), session_id, data_len);

    std::vector<uint8_t> vfs_packet;
    uint32_t magic = 0x5A484941;
    vfs_packet.insert(vfs_packet.end(), (uint8_t*)&magic, ((uint8_t*)&magic) + 4);
    vfs_packet.push_back(opcode);
    vfs_packet.insert(vfs_packet.end(), (uint8_t*)&session_id, ((uint8_t*)&session_id) + 8);
    vfs_packet.insert(vfs_packet.end(), (uint8_t*)&offset, ((uint8_t*)&offset) + 8);
    vfs_packet.insert(vfs_packet.end(), (uint8_t*)&data_len, ((uint8_t*)&data_len) + 4);
    vfs_packet.insert(vfs_packet.end(), (uint8_t*)&path_len, ((uint8_t*)&path_len) + 2);
    vfs_packet.insert(vfs_packet.end(), c_path, c_path + path_len);

    if (data != nullptr && opcode != 0x03) {
        vfs_packet.insert(vfs_packet.end(), c_data, c_data + data_len);
    }

    env->ReleaseStringUTFChars(path, c_path);
    if (data != nullptr) env->ReleaseByteArrayElements(data, c_data, JNI_ABORT);

    std::vector<uint8_t> ciphertext;
    if (!CryptoBox::encrypt_payload(vfs_packet, g_master_key, ciphertext)) {
        LOGE("❌ Mã hóa Libsodium thất bại! SessionID: %llu", session_id);
        return nullptr;
    }

    auto waiter = std::make_shared<RequestWait>();
    {
        std::lock_guard<std::mutex> lock(g_promise_mutex);
        g_pending_requests[session_id] = waiter;
    }

    {
        std::lock_guard<std::mutex> lock(g_kcp_mutex);
        if (g_kcp) {
            ikcp_send(g_kcp, (const char*)ciphertext.data(), ciphertext.size());
            ikcp_flush(g_kcp);
            LOGI("🚀 [SEND] Bơm thành công %zu bytes xuống ống KCP (SessionID: %llu)", ciphertext.size(), session_id);
        }
    }

    std::unique_lock<std::mutex> wait_lock(g_promise_mutex);
    if (waiter->cv.wait_for(wait_lock, std::chrono::seconds(25), [&]{ return waiter->done; })) {
        g_pending_requests.erase(session_id);
        jbyteArray retArray = env->NewByteArray(waiter->data.size());
        env->SetByteArrayRegion(retArray, 0, waiter->data.size(), (jbyte*)waiter->data.data());
        return retArray;
    } else {
        g_pending_requests.erase(session_id);
        LOGE("❌ [TIMEOUT] Lệnh 0x%02X SessionID %llu mất tín hiệu sau 25s!", opcode, session_id);
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_transfer_1server_KcpNative_shutdownKcp(JNIEnv *env, jobject thiz) {
    LOGI("🛑 Chuẩn bị Shutdown KCP, đánh thức các luồng đang đợi...");
    g_running = false;

    // 🔥 GIẢI PHÓNG DEADLOCK: Đánh thức tất cả các thread đang chờ Wait_For dậy để thoái lui!
    {
        std::lock_guard<std::mutex> lock(g_promise_mutex);
        for (auto& pair : g_pending_requests) {
            pair.second->done = true;
            pair.second->cv.notify_all();
        }
    }

    if (g_recv_thread.joinable()) g_recv_thread.join();
    if (g_update_thread.joinable()) g_update_thread.join();

    std::lock_guard<std::mutex> lock(g_kcp_mutex);
    if (g_kcp) { ikcp_release(g_kcp); g_kcp = nullptr; }
    if (g_socket_fd >= 0) { close(g_socket_fd); g_socket_fd = -1; }
    LOGI("🛑 SHUTDOWN C++ KCP ENGINE HOÀN TOÀN!");
}
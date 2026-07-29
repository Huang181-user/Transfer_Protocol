#include "client.h"
#include "logger.h"
#include "crypto_box.h"
#include "vfs_packet.h"
#include <thread>
#include <chrono>
#include <cstring>

ZhiClient::ZhiClient() : udp_socket(INVALID_SOCKET), kcp_cb(nullptr), is_running(false) {}
ZhiClient::~ZhiClient() { stop(); }

bool ZhiClient::start(const std::string& ip, int port, const std::string& key) {
    master_key = key;
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket == INVALID_SOCKET) {
        ZHI_LOG_ERR("Winsock: Khong tao duoc Socket UDP. Loi: " + std::to_string(WSAGetLastError()));
        return false;
    }
    u_long mode = 1;
    ioctlsocket(udp_socket, FIONBIO, &mode);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    kcp_cb = ikcp_create(0x11223344, this);
    kcp_cb->output = ZhiClient::kcp_output_cb;
    ikcp_nodelay(kcp_cb, 1, 10, 2, 1);
    ikcp_wndsize(kcp_cb, 8192, 8192);
    is_running = true;
    std::thread(&ZhiClient::run_update_loop, this).detach();
    ZHI_LOG_INFO("Duong ham KCP UDP toi Server [" + ip + ":" + std::to_string(port) + "] da san sang.");
    return true;
}

void ZhiClient::stop() {
    is_running = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (kcp_cb) { ikcp_release(kcp_cb); kcp_cb = nullptr; }
    if (udp_socket != INVALID_SOCKET) { closesocket(udp_socket); udp_socket = INVALID_SOCKET; }
    ZHI_LOG_INFO("Da dong ket noi KCP an toan.");
}

int ZhiClient::kcp_output_cb(const char* buf, int len, ikcpcb* kcp, void* user) {
    ZhiClient* client = static_cast<ZhiClient*>(user);
    sendto(client->udp_socket, buf, len, 0, (struct sockaddr*)&client->server_addr, sizeof(client->server_addr));
    return 0;
}

void ZhiClient::run_update_loop() {
    char buf[65536];
    int addr_len = sizeof(server_addr);
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        uint32_t current_clock = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        ikcp_update(kcp_cb, current_clock);
        int bytes = recvfrom(udp_socket, buf, sizeof(buf), 0, (struct sockaddr*)&server_addr, &addr_len);
        if (bytes > 0) {
            ikcp_input(kcp_cb, buf, bytes);
        }
    }
}

bool ZhiClient::test_connection() {
    ZHI_LOG_INFO("Bat dau gui goi tin PING bao mat test duong truyen...");
    VfsPacketHeader header;
    header.magic = 0x5A484941;
    header.opcode = VfsOpcode::OP_PING;
    header.session_id = 99999;
    header.offset = 0;
    header.data_len = 0;
    header.path_len = 0;
    std::vector<uint8_t> packet(sizeof(header));
    std::memcpy(packet.data(), &header, sizeof(header));
    std::vector<uint8_t> ciphertext;
    if (!CryptoBox::encrypt_payload(packet, master_key, ciphertext)) {
        ZHI_LOG_ERR("Ma hoa Libsodium that bai!");
        return false;
    }
    ikcp_send(kcp_cb, reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());
    ikcp_flush(kcp_cb);
    auto start_time = std::chrono::steady_clock::now();
    while (is_running) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count() > 4) {
            ZHI_LOG_ERR("Ket noi TIMEOUT! Server khong phan hoi hoac UFW dang chan.");
            return false;
        }
        int size = ikcp_peeksize(kcp_cb);
        if (size > 0) {
            std::vector<uint8_t> response_encrypted(size);
            ikcp_recv(kcp_cb, reinterpret_cast<char*>(response_encrypted.data()), size);
            std::vector<uint8_t> plaintext;
            if (CryptoBox::decrypt_payload(response_encrypted, master_key, plaintext)) {
                ZHI_LOG_INFO("THAC CONG! Da nhan phan hoi PONG hop le tu Server. Duong truyen hoat dong 100%!");
                return true;
            } else {
                ZHI_LOG_ERR("Bao dong: Giai ma phan hoi bi sai chu ky bao mat!");
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

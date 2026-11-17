#include "bridge/client_bridge.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_client.h"
#include "rpc_client/vfs_packet.h" 
#include <vector>
#include <string>
#include <cstring> // Cần cho memcpy

static VfsClient* g_vfs_client = nullptr;

extern "C" {

int zhiauth_client_init(const char* server_ip, int port, const char* sym_key, int mtu,
                        int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) {
    if (!CryptoBox::initialize()) return -1;
    if (g_vfs_client != nullptr) delete g_vfs_client;
    
    g_vfs_client = new VfsClient(server_ip, port, sym_key, mtu, nodelay, interval, resend, nc, snd_wnd, rcv_wnd);
    g_vfs_client->start();
    return 0;
}

void zhiauth_send_vfs_command_async(const uint8_t* payload, size_t payload_len) {
    if (g_vfs_client) {
        std::vector<uint8_t> req(payload, payload + payload_len);
        g_vfs_client->send_rpc_async(req); 
    }
}

void zhiauth_client_shutdown() {
    if (g_vfs_client) { 
        g_vfs_client->stop(); 
        delete g_vfs_client; 
        g_vfs_client = nullptr; 
    }
}

void* zhiauth_create_vfs_client(const char* ip, int port, const char* sym_key, int mtu,
                                int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) {
    if (!CryptoBox::initialize()) return nullptr;
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

// 🔥 FIX TẬN GỐC LỖI MEMORY ALIGNMENT: NẠP TỪNG BYTE BẰNG TAY (BYTE-BY-BYTE SERIALIZATION)
void zhiauth_vfs_send_rpc(void* client, uint32_t req_id, uint8_t opcode, const char* path, 
                          uint64_t offset, uint32_t req_len, const uint8_t* payload, uint32_t payload_len) {
    if (!client) return;
    
    uint16_t path_length = path ? static_cast<uint16_t>(strlen(path)) : 0;
    
    // Tính toán kích thước chuẩn: 27 bytes Header + Độ dài chuỗi Path + Độ dài mảng Payload
    size_t total_size = 27 + path_length + payload_len;
    std::vector<uint8_t> req_data(total_size, 0);
    
    // 1. Ghi Magic (0x5A484941)
    uint32_t magic = 0x5A484941;
    memcpy(&req_data[0], &magic, 4);
    
    // 2. Ghi Opcode
    req_data[4] = opcode;
    
    // 3. Ghi SessionID (Tạm gán req_id, vào vfs_client.cpp sẽ bị đắp đè HWID lên)
    uint64_t temp_session_id = req_id;
    memcpy(&req_data[5], &temp_session_id, 8);
    
    // 4. Ghi Offset
    memcpy(&req_data[13], &offset, 8);
    
    // 5. Ghi Data_len (Nếu là lệnh PING/STAT/LIST thì ghi 0, nếu READ thì ghi dung lượng cần đọc, WRITE thì ghi dung lượng mảng)
    uint32_t final_data_len = (req_len > 0) ? req_len : payload_len;
    memcpy(&req_data[21], &final_data_len, 4);
    
    // 6. Ghi Path_len
    memcpy(&req_data[25], &path_length, 2);
    
    // 7. Chèn chuỗi đường dẫn (Path)
    if (path_length > 0) {
        memcpy(&req_data[27], path, path_length);
    }
    
    // 8. Chèn dữ liệu Payload (nếu có, dùng cho OP_WRITE)
    if (payload && payload_len > 0) {
        memcpy(&req_data[27 + path_length], payload, payload_len);
    }
    
    // Quăng xuống lõi mạng KCP
    static_cast<VfsClient*>(client)->send_rpc_async(req_data);
}

}
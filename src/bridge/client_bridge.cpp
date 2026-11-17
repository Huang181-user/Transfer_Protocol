#include "bridge/client_bridge.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_client.h"
#include "rpc_client/vfs_packet.h" 
#include <vector>
#include <string>

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

// Struct truyền tải đã được chứng minh chuẩn xác 100%, không bị memory alignment padding!
void zhiauth_vfs_send_rpc(void* client, uint32_t req_id, uint8_t opcode, const char* path, 
                          uint64_t offset, uint32_t req_len, const uint8_t* payload, uint32_t payload_len) {
    if (!client) return;
    uint16_t path_length = path ? static_cast<uint16_t>(strlen(path)) : 0;
    std::vector<uint8_t> req_data(sizeof(VfsPacketHeader) + path_length + payload_len, 0);
    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req_data.data());
    
    hdr->magic = 0x5A484941;
    hdr->opcode = static_cast<VfsOpcode>(opcode);
    hdr->offset = offset;
    hdr->data_len = (req_len > 0) ? req_len : payload_len; 
    hdr->path_len = path_length;
    hdr->session_id = req_id; 
    
    uint8_t* ptr = req_data.data() + sizeof(VfsPacketHeader);
    if (path_length > 0) {
        memcpy(ptr, path, path_length);
        ptr += path_length;
    }
    if (payload && payload_len > 0) {
        memcpy(ptr, payload, payload_len);
    }
    
    static_cast<VfsClient*>(client)->send_rpc_async(req_data);
}
}
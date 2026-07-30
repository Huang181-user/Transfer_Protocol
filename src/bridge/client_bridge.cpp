#include "bridge/client_bridge.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_client.h"

static VfsClient* g_vfs_client = nullptr;

extern "C" {
int zhiauth_client_init(const char* server_ip, int port, const char* sym_key, int mtu) {
    if (!CryptoBox::initialize()) return -1;
    if (g_vfs_client != nullptr) delete g_vfs_client;
    g_vfs_client = new VfsClient(server_ip, port, sym_key, mtu);
    g_vfs_client->start();
    return 0;
}

void zhiauth_send_vfs_command_async(const uint8_t* payload, size_t payload_len) {
    if (g_vfs_client) {
        std::vector<uint8_t> req(payload, payload + payload_len);
        g_vfs_client->send_rpc_async(req); // Đổi thành Async!
    }
}

void zhiauth_client_shutdown() {
    if (g_vfs_client) { g_vfs_client->stop(); delete g_vfs_client; g_vfs_client = nullptr; }
}
}

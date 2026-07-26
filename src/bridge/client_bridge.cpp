#include "bridge/client_bridge.h"
#include "rpc_client/crypto_box.h"
#include "rpc_client/vfs_client.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>

static VfsClient* g_vfs_client = nullptr;

static std::string getBridgeTS() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
    std::tm bt{}; localtime_r(&time_t_now, &bt);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(6) << micros;
    return oss.str();
}

extern "C" {
int zhiauth_client_init(const char* server_ip, int port, const char* sym_key, int mtu) {
    if (!CryptoBox::initialize()) return -1;
    if (g_vfs_client != nullptr) delete g_vfs_client;
    g_vfs_client = new VfsClient(server_ip, port, sym_key, mtu);
    g_vfs_client->start();
    return 0;
}

uint8_t* zhiauth_send_vfs_command(const uint8_t* payload, size_t payload_len, size_t* out_len) {
    std::cout << "[" << getBridgeTS() << "] [STAGE 2.1: CGO_BRIDGE_ENTRY] Forwarding bytes to C++ Core. Size: " << payload_len << " bytes" << std::endl;
    if (!g_vfs_client) { *out_len = 0; return nullptr; }
    
    std::vector<uint8_t> req(payload, payload + payload_len);
    std::vector<uint8_t> res = g_vfs_client->send_rpc_sync(req);
    
    if (res.empty()) { 
        std::cout << "[" << getBridgeTS() << "] [STAGE 2.E: CGO_BRIDGE_ERROR] RPC Sync returned EMPTY response!" << std::endl;
        *out_len = 0; return nullptr; 
    }
    
    *out_len = res.size();
    uint8_t* out_buf = (uint8_t*)malloc(res.size());
    std::memcpy(out_buf, res.data(), res.size());
    std::cout << "[" << getBridgeTS() << "] [STAGE 2.2: CGO_BRIDGE_SUCCESS] Returning bytes to Go FUSE. Size: " << res.size() << " bytes" << std::endl;
    return out_buf;
}

void zhiauth_client_shutdown() {
    if (g_vfs_client) { g_vfs_client->stop(); delete g_vfs_client; g_vfs_client = nullptr; }
}
}

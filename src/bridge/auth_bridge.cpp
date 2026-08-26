#include "bridge/auth_bridge.h"
#include "common/sqlite_handler.h"
#include "system/ufw_manager.h"
#include "rpc_vfs/crypto_box.h"
#include "rpc_vfs/vfs_server.h"
#include "rpc_quic/msquic_server.h"
#include "common/logger.h"
#include <string>

static SQLiteHandler db_handler;
static UFWManager ufw_manager;
static VfsServer* kcp_server = nullptr;
static int g_kcp_port = 0;
static int g_quic_port = 0;
static int g_nodelay = 1, g_interval = 10, g_resend = 2, g_nc = 0, g_snd_wnd = 512, g_rcv_wnd = 512;

void vfs_register_ip_uds(const std::string& ip, const std::string& uds_path);

extern "C" {

int zhiauth_core_init(const char* db_path, const char* master_key, int auth_port, int kcp_port, int quic_port, const char* cert_path, const char* key_path, int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd) {
    g_kcp_port = kcp_port; 
    g_quic_port = quic_port;
    g_nodelay = nodelay; 
    g_interval = interval; 
    g_resend = resend; 
    g_nc = nc;
    g_snd_wnd = snd_wnd; 
    g_rcv_wnd = rcv_wnd;

    if (!db_handler.initialize_database(db_path ? db_path : "")) return -1;
    ufw_manager.start_worker();
    if (!CryptoBox::initialize()) return -1;
    
    kcp_server = new VfsServer(g_kcp_port, master_key ? master_key : "", g_nodelay, g_interval, g_resend, g_nc, g_snd_wnd, g_rcv_wnd);
    kcp_server->start();
    
    std::string str_cert = cert_path ? cert_path : "cert.pem";
    std::string str_key = key_path ? key_path : "key.pem";
    MsQuicServer::initialize(auth_port, quic_port, str_cert, str_key);
    
    return 0;
}

const char* zhiauth_authenticate_and_trigger(const char* username, const char* password, const char* client_lan, const char* client_ts, const char* client_hwid) {
    static std::string result_cache;
    std::string user = username ? username : ""; std::string pass = password ? password : ""; 
    std::string lan = client_lan ? client_lan : ""; std::string ts_ip = client_ts ? client_ts : "";
    
    UserRecord user_rec;
    if (!db_handler.authenticate_user(user, pass, user_rec)) return "0|AUTH_FAIL";

    std::string uds_path = "/tmp/zhiauth_kcp_" + user_rec.username + ".sock";

    if (!lan.empty() && lan != "NONE") {
        vfs_register_ip_uds(lan, uds_path);
        ufw_manager.push_task(lan, g_quic_port, "udp", true); 
        ufw_manager.push_task(lan, g_kcp_port, "udp", true); 
        ufw_manager.push_task(lan, 0, "icmp", true);
    }
    if (!ts_ip.empty() && ts_ip != "NONE") {
        vfs_register_ip_uds(ts_ip, uds_path);
        ufw_manager.push_task(ts_ip, g_quic_port, "udp", true); 
        ufw_manager.push_task(ts_ip, g_kcp_port, "udp", true); 
        ufw_manager.push_task(ts_ip, 0, "icmp", true);
    }
    result_cache = "1|" + user_rec.username + "|" + user_rec.shared_path;
    return result_cache.c_str();
}

void zhiauth_revoke_access(const char* client_ip, const char* shared_path) {
    std::string ip = client_ip ? client_ip : "";
    if (!ip.empty()) {
        ufw_manager.push_task(ip, g_quic_port, "udp", false); 
        ufw_manager.push_task(ip, g_kcp_port, "udp", false); 
        ufw_manager.push_task(ip, 0, "icmp", false);
    }
}

uint64_t zhiauth_check_kcp_active(const char* client_ip) {
    if (!kcp_server || !client_ip) return 0;
    return kcp_server->get_last_active_time(client_ip);
}

void zhiauth_core_shutdown() { 
    MsQuicServer::shutdown();
    if (kcp_server) { kcp_server->stop(); delete kcp_server; } 
    ufw_manager.stop_worker(); 
}

} // extern "C"

#include "common/logger.h"
#include "system/sys_utils.hpp"
#include "bridge/win_auth.h"
#include "rpc_quic/msquic_client.h"
#include "rpc_client/vfs_client.h"
#include "rpc_client/vfs_packet.h"
#include "vfs/fuse_driver.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

using json = nlohmann::json;

VfsClient* g_vfs_client = nullptr;

int get_port_safe(const json& j, const std::string& key, int def) {
    if (j.contains(key)) return j[key].is_string() ? std::stoi(j[key].get<std::string>()) : j[key].get<int>();
    return def;
}

std::string DiscoverBestRoute(const std::string& lan, const std::string& ts) {
    if (!lan.empty() && SysUtils::ping_test(lan, 100)) return lan;
    if (!ts.empty() && SysUtils::ping_test(ts, 100)) return ts;
    return lan;
}

int main() {
    ZHI_LOG_INFO("==================================================");
    ZHI_LOG_INFO("🚀 HUANG HYBRID C++ CLIENT v6.0 - FUSE API EDITION");
    ZHI_LOG_INFO("==================================================");

    std::ifstream f("config/config.json");
    if (!f.is_open()) {
        f.open("../config/config.json");
        if (!f.is_open()) { ZHI_LOG_ERR("Khong tim thay config.json!"); return 1; }
    }

    json config = json::parse(f);
    std::string server_lan_ip = config.value("server_lan_ip", "127.0.0.1");
    std::string server_ts_ip = config.value("server_ts_ip", "");
    int auth_port = get_port_safe(config, "auth_port", 5555);
    int quic_port = get_port_safe(config, "quic_data_port", 4433);
    int kcp_port = get_port_safe(config, "kcp_data_port", 6666);
    std::string kcp_key = config.value("kcp_key", "ZhiAuth_Secret_KCP_Key_2026_1234");
    
    std::string mount_kcp = config.value("mount_kcp_drive", "X") + ":";
    std::string mount_quic = config.value("mount_quic_drive", "Y") + ":";

    std::string lan_ip, ts_ip;
    SysUtils::auto_detect_ips(lan_ip, ts_ip);
    std::string activeIp = DiscoverBestRoute(server_lan_ip, server_ts_ip);

    int optimalMTU = SysUtils::discover_best_huang_mtu(activeIp);
    int kcpMtu = optimalMTU - 28;

    char user[256] = {0}, pass[256] = {0}; int save = 0;
    if (!win_load_cred(user, pass)) {
        if (!win_prompt_cred(user, pass, &save)) return 1;
        if (save) win_save_cred(user, pass);
    }

    std::string hwid = SysUtils::get_hardware_fingerprint();
    if (!MsQuicClient::initialize(activeIp, auth_port, quic_port)) return 1;

    std::string auth_payload = "AUTH_REQ|USER:" + std::string(user) + "|PASS:" + std::string(pass) + "|LAN:" + lan_ip + "|TS:" + ts_ip + "|HWID:" + hwid;
    std::string auth_res = MsQuicClient::auth_sync(auth_payload);

    if (auth_res.find("AUTH_SUCCESS|") == 0) {
        ZHI_LOG_INFO("🎉 XAC THUC THANH CONG! He thong chuan bi no may...");
        
        std::string remote_base = "/";
        size_t first_pipe = auth_res.find('|');
        if (first_pipe != std::string::npos) {
            size_t second_pipe = auth_res.find('|', first_pipe + 1);
            if (second_pipe != std::string::npos) {
                remote_base = auth_res.substr(first_pipe + 1, second_pipe - first_pipe - 1);
            }
        }
        
        ZHI_LOG_INFO("Danh thuc C++ KCP Engine (MTU: " + std::to_string(kcpMtu) + ")...");
        g_vfs_client = new VfsClient(activeIp, kcp_port, kcp_key, kcpMtu, 1, 10, 2, 1, 16384, 16384, hwid);
        g_vfs_client->start();

        ZHI_LOG_INFO("==========================================================================");
        ZHI_LOG_INFO("👉 Kich no he thong FUSE Dual-Mount len o dia " + mount_kcp + " va " + mount_quic);
        ZHI_LOG_INFO("==========================================================================");

        std::thread t1(FuseDriver::start_fuse, mount_kcp, remote_base, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::thread t2(FuseDriver::start_fuse, mount_quic, remote_base, false);

        std::thread hb_quic([&]() {
            while(true) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                std::vector<uint8_t> req(sizeof(VfsPacketHeader));
                VfsPacketHeader* hdr = (VfsPacketHeader*)req.data();
                hdr->magic = 0x5A484941; hdr->opcode = VfsOpcode::OP_PING;
                hdr->session_id = 9999;
                MsQuicClient::send_vfs_sync(req, 9999);
            }
        });

        std::thread hb_kcp([&]() {
            while(true) {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                std::vector<uint8_t> req(sizeof(VfsPacketHeader));
                VfsPacketHeader* hdr = (VfsPacketHeader*)req.data();
                hdr->magic = 0x5A484941; hdr->opcode = VfsOpcode::OP_PING;
                hdr->session_id = 9999;
                if (g_vfs_client) g_vfs_client->send_rpc_sync(req, 9999);
            }
        });

        // 🔥 FIX: Thêm user, pass, hwid vào lambda capture!
        SysUtils::start_network_monitor([user_str = std::string(user), pass_str = std::string(pass), hwid]() {
            ZHI_LOG_WARN("📡 Card mang thay doi! Tien hanh Re-Auth...");
            std::string l, t; SysUtils::auto_detect_ips(l, t);
            std::string p = "AUTH_REQ|USER:" + user_str + "|PASS:" + pass_str + "|LAN:" + l + "|TS:" + t + "|HWID:" + hwid;
            MsQuicClient::auth_sync(p);
        });

        ZHI_LOG_INFO("✅ Daemon running in background at MAXIMUM SPEED. Type 'exit' to quit, 'logout' to sign out.");

        std::string cmd;
        while (std::getline(std::cin, cmd)) {
            if (cmd == "exit" || cmd == "quit") {
                ZHI_LOG_INFO("[EXIT] Shutting down client safely...");
                system(("net use " + mount_kcp + " /delete /y 2>nul").c_str());
                system(("net use " + mount_quic + " /delete /y 2>nul").c_str());
                std::exit(0);
            }
            if (cmd == "logout") {
                ZHI_LOG_INFO("[LOGOUT] Logging out...");
                win_delete_cred();
                system(("net use " + mount_kcp + " /delete /y 2>nul").c_str());
                system(("net use " + mount_quic + " /delete /y 2>nul").c_str());
                std::exit(0);
            }
        }
        
        t1.join(); t2.join(); hb_quic.join(); hb_kcp.join();
    } else {
        ZHI_LOG_ERR("❌ Xác thuc that bai: " + auth_res);
    }

    MsQuicClient::shutdown();
    if (g_vfs_client) { g_vfs_client->stop(); delete g_vfs_client; }
    return 0;
}


#include "common/logger.h"
#include "system/sys_utils.hpp"
#include "rpc_client/vfs_client.h"
#include "rpc_quic/msquic_client.h"
#include "rpc_client/crypto_box.h"
#include "vfs/fuse_driver.h"
#include "rpc_client/vfs_packet.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <csignal>

using json = nlohmann::json;
VfsClient* g_vfs_client = nullptr;
std::string g_vfs_dir;
std::string g_quic_dir;

// 🔥 1. PHỤC HỒI: CƠ CHẾ GỠ Ổ ĐĨA AN TOÀN KHI BẤM CTRL+C
void handle_sigint(int sig) {
    ZHI_LOG_INFO("\n[SHUTDOWN] Đang dọn dẹp hệ thống. Tháo đĩa an toàn...");
    if (g_vfs_client) { g_vfs_client->stop(); }
    MsQuicClient::shutdown();
    if (!g_vfs_dir.empty()) system(("fusermount3 -u -z " + g_vfs_dir + " 2>/dev/null").c_str());
    if (!g_quic_dir.empty()) system(("fusermount3 -u -z " + g_quic_dir + " 2>/dev/null").c_str());
    ZHI_LOG_INFO("[EXIT] Đã ngắt kết nối an toàn. Bật bãi!");
    exit(0);
}

// 🔥 2. PHỤC HỒI: MẮT THẦN CANH GÁC CARD MẠNG (ROAMING)
void network_monitor_loop(const std::string& old_lan, const std::string& old_ts) {
    ZHI_LOG_INFO("[MONITOR] 👁️ Khởi động Mắt thần canh gác Card mạng Linux Engine (Chu kỳ: 3s)...");
    std::string curr_lan = old_lan, curr_ts = old_ts;
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::string new_lan, new_ts;
        SysUtils::auto_detect_ips(new_lan, new_ts);
        if (new_lan != curr_lan || new_ts != curr_ts) {
            ZHI_LOG_WARN("==========================================================================");
            ZHI_LOG_WARN("[⚡ ALERT][MONITOR] PHÁT HIỆN CARD MẠNG SYSTEM LINUX CÓ BIẾN ĐỘNG PHẦN CỨNG!");
            ZHI_LOG_WARN("==========================================================================");
            curr_lan = new_lan; curr_ts = new_ts;
        }
    }
}

int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    ZHI_LOG_INFO("==========================================================================");
    ZHI_LOG_INFO("🚀 HUANG PURE C++ CLIENT v6.0 - FULL NATIVE ARCHITECTURE");
    ZHI_LOG_INFO("==========================================================================");

    std::ifstream f("/home/huang/zhiauth_client/config/config.json");
    if (!f.is_open()) { ZHI_LOG_ERR("Không tìm thấy config.json"); return 1; }
    json cfg = json::parse(f);

    std::string lan_ip, ts_ip;
    SysUtils::auto_detect_ips(lan_ip, ts_ip);
    
    std::string srv_lan = SysUtils::resolve_host_ipv4(cfg["server_lan_ip"]);
    std::string srv_ts = SysUtils::resolve_host_ipv4(cfg["server_ts_ip"]);
    
    std::string active_ip = SysUtils::discover_best_route(srv_lan, srv_ts);
    if (active_ip.empty()) { ZHI_LOG_ERR("Không thể kết nối Server!"); return 1; }

    std::string user, pass, mount_path;
    bool has_session = false;

    // 🔥 3. PHỤC HỒI: QUẢN LÝ SESSION (LƯU MẬT KHẨU)
    std::string session_path = "/home/huang/zhiauth_client/config/.session";
    std::ifstream session_file(session_path);
    if (session_file.is_open()) {
        std::getline(session_file, user);
        std::getline(session_file, pass);
        std::getline(session_file, mount_path);
        session_file.close();
        if (!user.empty() && !pass.empty()) {
            has_session = true;
            ZHI_LOG_INFO("[SESSION] Đã nạp lại thẻ bài bảo mật. Xin chào, " + user);
        }
    }

    if (!has_session) {
        std::cout << "👤 Username: "; std::cin >> user;
        std::cout << "🔑 Password: "; std::cin >> pass;
        std::cout << "👉 Mount Point [/mnt/Cloud]: "; std::cin.ignore(); std::getline(std::cin, mount_path);
        if (mount_path.empty()) mount_path = "/mnt/Cloud";
    }

    std::string hwid = SysUtils::get_hardware_fingerprint();
    std::string auth_cmd = "AUTH_REQ|USER:" + user + "|PASS:" + pass + "|LAN:" + lan_ip + "|TS:" + ts_ip + "|HWID:" + hwid;

    int auth_port = std::stoi(cfg["auth_port"].get<std::string>());
    MsQuicClient::initialize(active_ip, auth_port, 4433);
    std::string auth_res = MsQuicClient::auth_sync(auth_cmd);

    std::vector<std::string> parts;
    std::stringstream ss(auth_res);
    std::string item;
    while (std::getline(ss, item, '|')) { parts.push_back(item); }

    if (parts.size() >= 10 && parts[0] == "AUTH_SUCCESS") {
        ZHI_LOG_INFO("✅ Đăng nhập MsQUIC thành công!");
        
        // Lưu lại phiên đăng nhập nếu chưa có
        if (!has_session) {
            std::ofstream out_session(session_path);
            out_session << user << "\n" << pass << "\n" << mount_path << "\n";
            out_session.close();
            ZHI_LOG_INFO("[SESSION] Đã lưu thẻ bài đăng nhập an toàn.");
        }

        std::string remote_path = parts[1];
        int quic_port = std::stoi(parts[2]);
        int kcp_port = std::stoi(parts[3]);
        int dyn_nodelay = std::stoi(parts[4]);
        int dyn_interval = std::stoi(parts[5]);
        int dyn_resend = std::stoi(parts[6]);
        int dyn_nc = std::stoi(parts[7]);
        int dyn_snd_wnd = std::stoi(parts[8]);
        int dyn_rcv_wnd = std::stoi(parts[9]);

        ZHI_LOG_INFO("[DYNAMIC-KCP] KCP Tuning từ Server: Wnd=" + std::to_string(dyn_snd_wnd) + " Interval=" + std::to_string(dyn_interval) + "ms");

        std::thread msquic_heartbeat([&]() {
            while(true) { std::this_thread::sleep_for(std::chrono::seconds(15)); MsQuicClient::auth_sync("AUTH_REQ|PING"); }
        });
        msquic_heartbeat.detach();

        int best_mtu = SysUtils::discover_best_mtu(active_ip);
        CryptoBox::initialize();
        
        g_vfs_client = new VfsClient(active_ip, kcp_port, cfg["master_sym_key"], best_mtu, dyn_nodelay, dyn_interval, dyn_resend, dyn_nc, dyn_snd_wnd, dyn_rcv_wnd);
        g_vfs_client->start();

        std::thread kcp_heartbeat([&]() {
            while(true) {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                if (g_vfs_client) {
                    std::vector<uint8_t> req(sizeof(VfsPacketHeader));
                    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req.data());
                    hdr->magic = 0x5A484941; hdr->opcode = VfsOpcode::OP_PING;
                    g_vfs_client->send_rpc_sync(req, 999999);
                }
            }
        });
        kcp_heartbeat.detach();

        std::thread net_monitor(network_monitor_loop, lan_ip, ts_ip);
        net_monitor.detach();

        g_vfs_dir = mount_path + "/VFS_DRIVE";
        g_quic_dir = mount_path + "/QUIC_DRIVE";
        system(("mkdir -p " + g_vfs_dir).c_str()); system(("mkdir -p " + g_quic_dir).c_str());
        system(("fusermount3 -u -z " + g_vfs_dir + " 2>/dev/null").c_str()); system(("fusermount3 -u -z " + g_quic_dir + " 2>/dev/null").c_str());

        std::thread t1([=]() { FuseDriver::start_fuse(g_vfs_dir, remote_path, true); });
        std::thread t2([=]() { FuseDriver::start_fuse(g_quic_dir, remote_path, false); });
        
        ZHI_LOG_INFO("Trục kép đã mount tại " + g_vfs_dir + " và " + g_quic_dir + ". Bấm Ctrl+C để ngắt kết nối.");
        t1.join(); t2.join();
        
    } else { 
        ZHI_LOG_ERR("❌ Sai thông tin đăng nhập hoặc rớt mạng."); 
        remove(session_path.c_str());
    }

    if (g_vfs_client) { g_vfs_client->stop(); delete g_vfs_client; }
    MsQuicClient::shutdown();
    return 0;
}

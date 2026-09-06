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
#include <termios.h>

using json = nlohmann::json;
VfsClient *g_vfs_client = nullptr;
std::string g_vfs_dir;
std::string g_quic_dir;

void handle_sigint(int sig)
{
    ZHI_LOG_INFO("\n[SHUTDOWN] Đang dọn dẹp hệ thống. Tháo đĩa an toàn...");
    if (g_vfs_client)
    {
        g_vfs_client->stop();
    }
    MsQuicClient::shutdown();
    if (!g_vfs_dir.empty())
        system(("fusermount3 -u -z " + g_vfs_dir + " 2>/dev/null").c_str());
    if (!g_quic_dir.empty())
        system(("fusermount3 -u -z " + g_quic_dir + " 2>/dev/null").c_str());
    ZHI_LOG_INFO("[EXIT] Đã ngắt kết nối an toàn. Bật bãi!");
    exit(0);
}

void network_monitor_loop(const std::string &old_lan, const std::string &old_ts)
{
    ZHI_LOG_INFO("[MONITOR] 👁️ Khởi động Mắt thần canh gác Card mạng Linux Engine (Chu kỳ: 3s)...");
    std::string curr_lan = old_lan, curr_ts = old_ts;
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::string new_lan, new_ts;
        SysUtils::auto_detect_ips(new_lan, new_ts);
        if (new_lan != curr_lan || new_ts != curr_ts)
        {
            ZHI_LOG_WARN("==========================================================================");
            ZHI_LOG_WARN("[⚡ ALERT][MONITOR] PHÁT HIỆN CARD MẠNG SYSTEM LINUX CÓ BIẾN ĐỘNG PHẦN CỨNG!");
            ZHI_LOG_WARN("==========================================================================");
            curr_lan = new_lan;
            curr_ts = new_ts;
        }
    }
}

int main()
{
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    ZHI_LOG_INFO("==========================================================================");
    ZHI_LOG_INFO("🚀 HUANG PURE C++ CLIENT v6.0 - FULL NATIVE ARCHITECTURE");
    ZHI_LOG_INFO("==========================================================================");

    std::ifstream f("/home/huang/zhiauth_client/config/config.json");
    if (!f.is_open())
    {
        ZHI_LOG_ERR("Không tìm thấy config.json");
        return 1;
    }
    json cfg = json::parse(f);

    std::string lan_ip, ts_ip;
    SysUtils::auto_detect_ips(lan_ip, ts_ip);

    std::string srv_lan = SysUtils::resolve_host_ipv4(cfg["server_lan_ip"]);
    std::string srv_ts = SysUtils::resolve_host_ipv4(cfg["server_ts_ip"]);

    std::string active_ip = SysUtils::discover_best_route(srv_lan, srv_ts);
    if (active_ip.empty())
    {
        ZHI_LOG_ERR("Không thể kết nối Server!");
        return 1;
    }

    std::string user, pass, mount_path;
    bool has_session = false;

    std::string session_path = "/home/huang/zhiauth_client/config/.session";
    std::string master_key = cfg["master_sym_key"];

    // Bật động cơ Libsodium sớm để thao tác với file session
    CryptoBox::initialize();

    std::ifstream session_file(session_path, std::ios::binary | std::ios::ate);
    if (session_file.is_open())
    {
        std::streamsize size = session_file.tellg();
        session_file.seekg(0, std::ios::beg);
        std::vector<uint8_t> enc_data(size);
        if (session_file.read((char *)enc_data.data(), size))
        {
            std::vector<uint8_t> dec_data;
            if (CryptoBox::decrypt_payload(enc_data, master_key, dec_data))
            {
                std::string session_str(dec_data.begin(), dec_data.end());
                std::stringstream ss(session_str);
                std::getline(ss, user);
                std::getline(ss, pass);
                std::getline(ss, mount_path);
                if (!user.empty() && !pass.empty())
                {
                    has_session = true;
                    ZHI_LOG_INFO("[SESSION] Đã nạp và giải mã thẻ bài bảo mật. Xin chào, " + user);
                }
            }
            else
            {
                ZHI_LOG_WARN("[SESSION] Lỗi giải mã! File session bị can thiệp hoặc sai khóa.");
            }
        }
        session_file.close();
    }

    if (!has_session)
    {
        std::cout << "👤 Username: ";
        std::cin >> user;
        std::cout << "🔑 Password: ";

        // Tắt echo trên Terminal để giấu mật khẩu
        termios oldt;
        tcgetattr(STDIN_FILENO, &oldt);
        termios newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        std::cin >> pass;
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << std::endl;

        std::cout << "👉 Mount Point [/mnt/Cloud]: ";
        std::cin.ignore();
        std::getline(std::cin, mount_path);
        if (mount_path.empty())
            mount_path = "/mnt/Cloud";
    }

    std::string hwid = SysUtils::get_hardware_fingerprint();
    std::string auth_cmd = "AUTH_REQ|USER:" + user + "|PASS:" + pass + "|LAN:" + lan_ip + "|TS:" + ts_ip + "|HWID:" + hwid;

    int auth_port = std::stoi(cfg["auth_port"].get<std::string>());
    MsQuicClient::initialize(active_ip, auth_port, 4433);
    std::string auth_res = MsQuicClient::auth_sync(auth_cmd);

    std::vector<std::string> parts;
    std::stringstream ss(auth_res);
    std::string item;
    while (std::getline(ss, item, '|'))
    {
        parts.push_back(item);
    }

    if (parts.size() >= 10 && parts[0] == "AUTH_SUCCESS")
    {
        ZHI_LOG_INFO("✅ Đăng nhập MsQUIC thành công!");

        if (!has_session)
        {
            std::string session_str = user + "\n" + pass + "\n" + mount_path + "\n";
            std::vector<uint8_t> plain_data(session_str.begin(), session_str.end());
            std::vector<uint8_t> enc_data;
            if (CryptoBox::encrypt_payload(plain_data, master_key, enc_data))
            {
                std::ofstream out_session(session_path, std::ios::binary);
                out_session.write((char *)enc_data.data(), enc_data.size());
                out_session.close();
                ZHI_LOG_INFO("[SESSION] Đã mã hóa và khóa cứng thẻ bài vào hệ thống.");
            }
        }

        std::string remote_path = parts[1];
        int s_quic_port = 4433, s_kcp_port = 6666;
        int s_nodelay = 1, s_interval = 1, s_resend = 2, s_nc = 1, s_snd_wnd = 16384, s_rcv_wnd = 16384;

        if (parts.size() >= 10)
        {
            s_quic_port = std::stoi(parts[2]);
            s_kcp_port = std::stoi(parts[3]);
            s_nodelay = std::stoi(parts[4]);
            s_interval = std::stoi(parts[5]);
            s_resend = std::stoi(parts[6]);
            s_nc = std::stoi(parts[7]);
            s_snd_wnd = std::stoi(parts[8]);
            s_rcv_wnd = std::stoi(parts[9]);
            ZHI_LOG_INFO("✅ Đã đồng bộ 8 thông số KCP/QUIC từ Server thành công!");
        }

        int best_mtu = SysUtils::discover_best_huang_mtu(active_ip);

        // 🔥 Thêm hwid vào cuối cùng
        g_vfs_client = new VfsClient(active_ip, s_kcp_port, master_key, best_mtu, s_nodelay, s_interval, s_resend, s_nc, s_snd_wnd, s_rcv_wnd, hwid);

        g_vfs_client->start();

        std::thread combined_watchdog([&, auth_cmd]()
                                      {
            int fail_count = 0;
            while(true) {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                std::string quic_res = MsQuicClient::auth_sync("AUTH_REQ|PING");
                
                bool kcp_ok = false;
                if (g_vfs_client) {
                    std::vector<uint8_t> req(sizeof(VfsPacketHeader));
                    VfsPacketHeader* hdr = reinterpret_cast<VfsPacketHeader*>(req.data());
                    hdr->magic = 0x5A484941; hdr->opcode = VfsOpcode::OP_PING; 
                    hdr->session_id = 999999; 
                    auto res = g_vfs_client->send_rpc_sync(req, 999999);
                    kcp_ok = !res.empty();
                }

                if ((quic_res == "AUTH_FAILED" || quic_res.empty()) && !kcp_ok) {
                    fail_count++;
                    if (fail_count >= 2) {
                        ZHI_LOG_WARN("[AUTO-RECONNECT] Mạng ngủ đông hoặc rớt NAT! Đang tái kích hoạt Port Knocking...");
                        MsQuicClient::auth_sync(auth_cmd);
                        fail_count = 0;
                    }
                } else { fail_count = 0; }
            } });
        combined_watchdog.detach();

        std::thread net_monitor(network_monitor_loop, lan_ip, ts_ip);
        net_monitor.detach();

        g_vfs_dir = mount_path + "/VFS_DRIVE";
        g_quic_dir = mount_path + "/QUIC_DRIVE";
        system(("mkdir -p " + g_vfs_dir).c_str());
        system(("mkdir -p " + g_quic_dir).c_str());
        system(("fusermount3 -u -z " + g_vfs_dir + " 2>/dev/null").c_str());
        system(("fusermount3 -u -z " + g_quic_dir + " 2>/dev/null").c_str());

        std::thread t1([=]()
                       { FuseDriver::start_fuse(g_vfs_dir, remote_path, true); });
        std::thread t2([=]()
                       { FuseDriver::start_fuse(g_quic_dir, remote_path, false); });

        ZHI_LOG_INFO("Trục kép đã mount tại " + g_vfs_dir + " và " + g_quic_dir + ". Bấm Ctrl+C để ngắt kết nối.");
        ZHI_LOG_INFO("Gõ 'logout' để đăng xuất, hoặc bấm Ctrl+C để ngắt kết nối.");

        std::string cmd;
        while (std::getline(std::cin, cmd))
        {
            if (cmd == "exit" || cmd == "quit")
            {
                handle_sigint(0);
            }
            if (cmd == "logout")
            {
                ZHI_LOG_INFO("[LOGOUT] Logging out...");
                remove(session_path.c_str());
                handle_sigint(0);
            }
        }

        t1.join();
        t2.join();
    }
    else
    {
        ZHI_LOG_ERR("❌ Sai thông tin đăng nhập hoặc rớt mạng.");
        remove(session_path.c_str());
    }

    if (g_vfs_client)
    {
        g_vfs_client->stop();
        delete g_vfs_client;
    }
    MsQuicClient::shutdown();
    return 0;
}

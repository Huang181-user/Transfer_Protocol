#include "common/logger.h"
#include "bridge/auth_bridge.h"
#include "system/ufw_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <sodium.h>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstdlib>
#include <algorithm>

using json = nlohmann::json;

struct UserContext {
    std::string username;
    std::string shared_path;
    std::string lan_ip;
    std::string ts_ip;
    uint64_t last_active;
};

static std::unordered_map<std::string, std::shared_ptr<UserContext>> g_session_cache;
static std::mutex g_session_mtx;
static json g_config;
static UFWManager ufw_manager;

extern "C" const char* go_get_username_by_ip(const char* ip) {
    std::lock_guard<std::mutex> lock(g_session_mtx);
    auto it = g_session_cache.find(ip);
    if (it != g_session_cache.end()) {
        it->second->last_active = time(NULL);
        return strdup(it->second->username.c_str());
    }
    return nullptr;
}

std::string hash_sha256(const std::string& input) {
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, (const unsigned char*)input.c_str(), input.length());
    char hex[crypto_hash_sha256_BYTES * 2 + 1];
    for(int i=0; i<crypto_hash_sha256_BYTES; i++) snprintf(hex + i*2, sizeof(hex) - i*2, "%02x", hash[i]);
    return std::string(hex);
}

std::string extract_field(const std::string& payload, const std::string& key) {
    size_t pos = payload.find(key + ":");
    if (pos == std::string::npos) return "";
    pos += key.length() + 1;
    size_t end = payload.find("|", pos);
    if (end == std::string::npos) end = payload.length();
    return payload.substr(pos, end - pos);
}

extern "C" const char* go_on_msquic_auth(const char* payload, const char* remote_ip) {
    std::string p(payload);
    if (p == "AUTH_REQ|PING") {
        std::lock_guard<std::mutex> lock(g_session_mtx);
        auto it = g_session_cache.find(remote_ip);
        // 🔥 VÁ TỬ HUYỆT: Chỉ trả lời PONG nếu Session còn sống. Nếu chết phải đuổi cổ!
        if (it != g_session_cache.end()) {
            it->second->last_active = time(NULL);
            return strdup("PONG");
        } else {
            return strdup("AUTH_FAILED");
        }
    }

    if (p.find("AUTH_REQ|") == 0) {
        std::string user = extract_field(p, "USER");
        std::string pass = extract_field(p, "PASS");
        std::string lan = extract_field(p, "LAN");
        std::string ts = extract_field(p, "TS");
        std::string hwid = extract_field(p, "HWID");

        std::string salt = g_config["security"]["hash_salt"].get<std::string>();
        std::string hashed_pass = hash_sha256(pass + salt);

        const char* res = zhiauth_authenticate_and_trigger(user.c_str(), hashed_pass.c_str(), lan.c_str(), ts.c_str(), hwid.c_str());
        std::string res_str = res;

        if (res_str.find("1|") == 0) {
            size_t first = res_str.find('|');
            size_t second = res_str.find('|', first + 1);
            std::string dbUser = res_str.substr(first + 1, second - first - 1);
            std::string dbPath = res_str.substr(second + 1);

            std::string uds_path = "/tmp/zhiauth_kcp_" + dbUser + ".sock";
            int test_sock = socket(AF_UNIX, SOCK_STREAM, 0);
            struct sockaddr_un addr; memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX; strncpy(addr.sun_path, uds_path.c_str(), sizeof(addr.sun_path) - 1);
            bool is_alive = (connect(test_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
            close(test_sock);

            if (!is_alive) {
                remove(uds_path.c_str());
                std::string cmd = "sudo -n -u " + dbUser + " /usr/local/bin/zhiauth_kcp_worker " + uds_path + " > /tmp/zhiauth_worker_" + dbUser + ".log 2>&1 &";
                system(cmd.c_str());
                ZHI_LOG_INFO("[MULTI-CLIENT] Đã spawn KCP Worker cho user: " + dbUser);
            }

            {
                std::lock_guard<std::mutex> lock(g_session_mtx);
                auto ctx = std::make_shared<UserContext>(UserContext{dbUser, dbPath, lan, ts, (uint64_t)time(NULL)});
                if (!lan.empty() && lan != "NONE") g_session_cache[lan] = ctx;
                if (!ts.empty() && ts != "NONE") g_session_cache[ts] = ctx;
                g_session_cache[remote_ip] = ctx;
            }

            int quic_port = g_config["network"]["quic_data_port"].get<int>();
            int kcp_port = g_config["network"]["kcp_data_port"].get<int>();
            if (!lan.empty() && lan != "NONE") { ufw_manager.push_task(lan, quic_port, "udp", true); ufw_manager.push_task(lan, kcp_port, "udp", true); }
            if (!ts.empty() && ts != "NONE") { ufw_manager.push_task(ts, quic_port, "udp", true); ufw_manager.push_task(ts, kcp_port, "udp", true); }

            char resp[512];
            snprintf(resp, sizeof(resp), "AUTH_SUCCESS|%s|%d|%d|%d|%d|%d|%d|%d|%d",
                dbPath.c_str(), quic_port, kcp_port,
                g_config["kcp_tuning"]["nodelay"].get<int>(), g_config["kcp_tuning"]["interval"].get<int>(),
                g_config["kcp_tuning"]["resend"].get<int>(), g_config["kcp_tuning"]["nc"].get<int>(),
                g_config["kcp_tuning"]["snd_wnd"].get<int>(), g_config["kcp_tuning"]["rcv_wnd"].get<int>()
            );
            return strdup(resp);
        }
    }
    return strdup("AUTH_FAILED");
}

void watchdog_loop() {
    ZHI_LOG_INFO("[WATCHDOG] Chó canh gác C++ đã thức giấc (Tuần tra mỗi 30s)");
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        uint64_t now = time(NULL);
        std::unordered_map<std::string, std::string> to_kill;
        {
            std::lock_guard<std::mutex> lock(g_session_mtx);
            for (auto it = g_session_cache.begin(); it != g_session_cache.end(); ++it) {
                uint64_t kcp_lan = zhiauth_check_kcp_active(it->second->lan_ip.c_str());
                uint64_t kcp_ts = zhiauth_check_kcp_active(it->second->ts_ip.c_str());
                uint64_t last_act = it->second->last_active;
                if (kcp_lan > last_act) last_act = kcp_lan;
                if (kcp_ts > last_act) last_act = kcp_ts;
                if (now - last_act > 120) to_kill[it->second->username] = it->second->shared_path;
            }
        }
        int quic_port = g_config["network"]["quic_data_port"].get<int>();
        int kcp_port = g_config["network"]["kcp_data_port"].get<int>();
        for (const auto& kv : to_kill) {
            ZHI_LOG_WARN("[WATCHDOG-ALERT] Mất tín hiệu user '" + kv.first + "'. Tiến hành càn quét!");
            std::string cmd = "sudo pkill -9 -u " + kv.first + " -f zhiauth_kcp_worker";
            system(cmd.c_str());
            remove(("/tmp/zhiauth_kcp_" + kv.first + ".sock").c_str());
            std::lock_guard<std::mutex> lock(g_session_mtx);
            for (auto it = g_session_cache.begin(); it != g_session_cache.end(); ) {
                if (it->second->username == kv.first) {
                    ufw_manager.push_task(it->second->lan_ip, quic_port, "udp", false); ufw_manager.push_task(it->second->lan_ip, kcp_port, "udp", false);
                    ufw_manager.push_task(it->second->ts_ip, quic_port, "udp", false); ufw_manager.push_task(it->second->ts_ip, kcp_port, "udp", false);
                    it = g_session_cache.erase(it);
                } else { ++it; }
            }
        }
    }
}

int main() {
    std::ifstream f("/home/huang/zhiauth/config/config.json");
    if (!f.is_open()) return 1;
    g_config = json::parse(f);
    ZHI_LOG_INFO("🚀 ZHIAUTH PURE C++ DAEMON v6.0 - TỐI ĐA HÓA SỨC MẠNH VẬT LÝ");
    std::string db_path = "/home/huang/zhiauth/" + g_config["paths"]["database"].get<std::string>();
    std::string master_key = g_config["security"]["master_sym_key"].get<std::string>();
    std::string crt_path = "/home/huang/zhiauth/config/zhiserver.tailc979c1.ts.net.crt";
    std::string key_path = "/home/huang/zhiauth/config/zhiserver.tailc979c1.ts.net.key";
    
    ufw_manager.start_worker();
    zhiauth_core_init(db_path.c_str(), master_key.c_str(), g_config["network"]["auth_port"].get<int>(), g_config["network"]["kcp_data_port"].get<int>(), g_config["network"]["quic_data_port"].get<int>(), crt_path.c_str(), key_path.c_str(), g_config["kcp_tuning"]["nodelay"].get<int>(), g_config["kcp_tuning"]["interval"].get<int>(), g_config["kcp_tuning"]["resend"].get<int>(), g_config["kcp_tuning"]["nc"].get<int>(), g_config["kcp_tuning"]["snd_wnd"].get<int>(), g_config["kcp_tuning"]["rcv_wnd"].get<int>());

    std::thread watchdog(watchdog_loop);
    watchdog.join(); 
    return 0;
}

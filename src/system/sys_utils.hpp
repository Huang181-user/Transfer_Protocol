#pragma once
#include <string>
#include <vector>

class SysUtils {
public:
    static std::string get_hardware_fingerprint();
    static void auto_detect_ips(std::string& out_lan, std::string& out_ts);
    static int discover_best_mtu(const std::string& target_ip);
    static std::string discover_best_route(const std::string& lan_ip, const std::string& ts_ip);
    static std::string resolve_host_ipv4(const std::string& host);
};

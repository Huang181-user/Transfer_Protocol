#ifndef SYS_UTILS_HPP
#define SYS_UTILS_HPP

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

class SysUtils {
public:
    static void auto_detect_ips(std::string& lan_ip, std::string& ts_ip);
    static std::string get_hardware_fingerprint();
    static bool ping_test(const std::string& ip, int targetSize);
    static int discover_best_huang_mtu(const std::string& target_ip);
    static std::string resolve_host_ipv4(const std::string& host);
    static void start_network_monitor(std::function<void()> on_change_callback);
};

#endif

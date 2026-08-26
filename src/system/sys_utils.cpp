#include "system/sys_utils.hpp"
#include "common/logger.h"
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <netdb.h>

std::string SysUtils::get_hardware_fingerprint() {
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];
    int success = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1) return "UNKNOWN_HW_ID";

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) { close(sock); return "UNKNOWN_HW_ID"; }

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
    std::string mac_str = "UNKNOWN_HW_ID";

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (!(ifr.ifr_flags & IFF_LOOPBACK)) {
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    char mac[18];
                    unsigned char* ptr = (unsigned char*)&ifr.ifr_hwaddr.sa_data;
                    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
                    mac_str = mac;
                    if (mac_str != "00:00:00:00:00:00") break;
                }
            }
        }
    }
    close(sock);
    ZHI_LOG_INFO("[SYS-HW] Trích xuất vân tay phần cứng (MAC): " + mac_str);
    return mac_str;
}

void SysUtils::auto_detect_ips(std::string& out_lan, std::string& out_ts) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip, INET_ADDRSTRLEN);
        std::string name(ifa->ifa_name);

        if (name.find("tailscale") != std::string::npos || name.find("ts") == 0) {
            out_ts = ip;
            ZHI_LOG_INFO("[SYS-NET] Dò thấy Tailscale WAN IP: " + out_ts + " (Card: " + name + ")");
        } else if (name.find("docker") == std::string::npos && name.find("veth") == std::string::npos) {
            out_lan = ip;
            ZHI_LOG_INFO("[SYS-NET] Dò thấy LAN IP: " + out_lan + " (Card: " + name + ")");
        }
    }
    freeifaddrs(ifaddr);
}

int SysUtils::discover_best_mtu(const std::string& target_ip) {
    ZHI_LOG_INFO("[MTU-RADAR] Kích hoạt hệ thống trinh sát MTU đến " + target_ip);
    int best_mtu = 1000;
    for (int mtu = 1500; mtu >= 1000; mtu -= 10) {
        int payload = mtu - 28;
        std::string cmd = "ping -c 1 -W 1 -M do -s " + std::to_string(payload) + " " + target_ip + " > /dev/null 2>&1";
        if (system(cmd.c_str()) == 0) {
            best_mtu = mtu;
            ZHI_LOG_INFO("[MTU-RADAR] Phân mảnh hoàn hảo ở mốc MTU: " + std::to_string(best_mtu) + " bytes.");
            break;
        }
        ZHI_LOG_DEBUG("[MTU-RADAR] Gói " + std::to_string(mtu) + " bị rớt, thử hạ trần...");
    }
    return best_mtu;
}

std::string SysUtils::discover_best_route(const std::string& lan_ip, const std::string& ts_ip) {
    auto ping_test = [](const std::string& ip) {
        if (ip.empty() || ip == "NONE") return false;
        std::string cmd = "ping -c 1 -W 1 " + ip + " > /dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    };

    if (ping_test(lan_ip)) return lan_ip;
    if (ping_test(ts_ip)) return ts_ip;
    return lan_ip.empty() ? ts_ip : lan_ip;
}

std::string SysUtils::resolve_host_ipv4(const std::string& host) {
    if (host.empty() || host == "NONE") return host;
    struct hostent* he = gethostbyname(host.c_str());
    if (he && he->h_addr_list[0]) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, he->h_addr_list[0], ip, sizeof(ip));
        ZHI_LOG_INFO("[DNS-OK] Đã dịch DNS '" + host + "' -> " + std::string(ip));
        return std::string(ip);
    }
    return host;
}

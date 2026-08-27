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
    struct ifreq ifr; struct ifconf ifc; char buf[1024];
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1) return "UNKNOWN_HW_ID";
    ifc.ifc_len = sizeof(buf); ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) { close(sock); return "UNKNOWN_HW_ID"; }
    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
    std::string mac_str = "UNKNOWN_HW_ID";
    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_LOOPBACK)) {
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                char mac[18]; unsigned char* ptr = (unsigned char*)&ifr.ifr_hwaddr.sa_data;
                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
                mac_str = mac; if (mac_str != "00:00:00:00:00:00") break;
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
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip, INET_ADDRSTRLEN);
        std::string name(ifa->ifa_name);
        if (name.find("tailscale") != std::string::npos || name.find("ts") == 0) out_ts = ip;
        else if (name.find("docker") == std::string::npos && name.find("veth") == std::string::npos) out_lan = ip;
    }
    freeifaddrs(ifaddr);
}

bool SysUtils::ping_test(const std::string& ip, int targetSize) {
    int payloadSize = targetSize - 28; if (payloadSize < 0) payloadSize = 0;
    std::string cmd = "ping -c 1 -W 1 -M do -s " + std::to_string(payloadSize) + " " + ip + " > /dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

int SysUtils::discover_best_huang_mtu(const std::string& target_ip) {
    ZHI_LOG_INFO("==========================================================================");
    ZHI_LOG_INFO("📡 KÍCH HOẠT HỆ THỐNG TRINH SÁT MTU ĐỘNG [HOÀNG-HEURISTIC-ALGORITHM]");
    ZHI_LOG_INFO("==========================================================================");
    if (ping_test(target_ip, 1500)) {
        ZHI_LOG_INFO("[SUCCESS][MTU-RADAR] 🎉 Tuyệt vời! Đường truyền thông suốt hoàn hảo ở mốc cực đại 1500 bytes! Không cần hạ trần.");
        return 1500;
    }
    ZHI_LOG_WARN("[WARNING][MTU-RADAR] 💥 Mốc 1500 bytes tịt ngòi! Hạ tầng dính nghẽn mạch hoặc bóp gói. Kích hoạt chia đôi phân đoạn...");
    int currentUpper = 1500, currentLower = 1000;
    while (true) {
        if (currentUpper - currentLower <= 1) return 1000;
        int distance = (currentUpper - currentLower) / 2; int mid = currentLower + distance;
        ZHI_LOG_INFO("[MTU-RADAR][CHIA-ĐÔI] -> Thử nghiệm mốc trung vị: " + std::to_string(mid) + " bytes...");
        if (ping_test(target_ip, mid)) {
            int lastSuccess = mid;
            ZHI_LOG_INFO("[MTU-RADAR][LEO-THANG] 📈 Kích nổ tiến trình quét leo thang hàng TRĂM/CHỤC/ĐƠN VỊ từ mốc " + std::to_string(mid) + "...");
            for (int val = lastSuccess + 100; val < currentUpper; val += 100) { if (ping_test(target_ip, val)) lastSuccess = val; else { currentUpper = val; break; } }
            for (int val = lastSuccess + 10; val < currentUpper; val += 10) { if (ping_test(target_ip, val)) lastSuccess = val; else { currentUpper = val; break; } }
            for (int val = lastSuccess + 1; val < currentUpper; val++) { if (ping_test(target_ip, val)) lastSuccess = val; else break; }
            ZHI_LOG_INFO("🏆 THUẬT TOÁN KẾT THÚC! ĐÃ TÌM RA ĐỈNH MTU TỐI ƯU: " + std::to_string(lastSuccess) + " bytes");
            return lastSuccess;
        } else {
            ZHI_LOG_WARN("[BURST][CHIA-ĐÔI] -> Gói " + std::to_string(mid) + " bytes bị rớt dọc đường. Co cụm giới hạn trên...");
            currentUpper = mid;
        }
    }
}

std::string SysUtils::discover_best_route(const std::string& lan_ip, const std::string& ts_ip) {
    auto ping = [](const std::string& ip) { return !ip.empty() && ip != "NONE" && system(("ping -c 1 -W 1 " + ip + " > /dev/null 2>&1").c_str()) == 0; };
    if (ping(lan_ip)) return lan_ip;
    if (ping(ts_ip)) return ts_ip;
    return lan_ip.empty() ? ts_ip : lan_ip;
}

std::string SysUtils::resolve_host_ipv4(const std::string& host) {
    if (host.empty() || host == "NONE") return host;
    struct hostent* he = gethostbyname(host.c_str());
    if (he && he->h_addr_list[0]) {
        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, he->h_addr_list[0], ip, sizeof(ip));
        ZHI_LOG_INFO("[DNS-OK] Đã dịch DNS '" + host + "' -> " + std::string(ip));
        return std::string(ip);
    }
    return host;
}

#include "system/sys_utils.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <thread>
#include "common/logger.h"

std::string SysUtils::resolve_host_ipv4(const std::string& host) {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
    struct addrinfo hints = {0}, *res = nullptr; hints.ai_family = AF_INET;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr, ipStr, sizeof(ipStr));
        freeaddrinfo(res); return std::string(ipStr);
    }
    return host;
}

void SysUtils::auto_detect_ips(std::string& lan_ip, std::string& ts_ip) {
    ULONG outBufLen = 15000; std::vector<uint8_t> buf(outBufLen);
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, (PIP_ADAPTER_ADDRESSES)buf.data(), &outBufLen) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_ADDRESSES addr = (PIP_ADAPTER_ADDRESSES)buf.data(); addr != NULL; addr = addr->Next) {
            if (addr->OperStatus != IfOperStatusUp || addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            char fn[256] = {0}; if (addr->FriendlyName) WideCharToMultiByte(CP_UTF8, 0, addr->FriendlyName, -1, fn, 256, NULL, NULL);
            std::string friendlyName = fn; std::string ip;
            if (addr->FirstUnicastAddress && addr->FirstUnicastAddress->Address.lpSockaddr) {
                sockaddr_in* sa = (sockaddr_in*)addr->FirstUnicastAddress->Address.lpSockaddr;
                char ipBuf[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &(sa->sin_addr), ipBuf, INET_ADDRSTRLEN); ip = ipBuf;
            }
            std::transform(friendlyName.begin(), friendlyName.end(), friendlyName.begin(), ::tolower);
            if (friendlyName.find("tailscale") != std::string::npos || friendlyName == "ts0") ts_ip = ip;
            else if (friendlyName.find("wi-fi") != std::string::npos || friendlyName.find("ethernet") != std::string::npos) lan_ip = ip;
        }
    }
}

std::string SysUtils::get_hardware_fingerprint() {
    ULONG outBufLen = 15000; std::vector<uint8_t> buf(outBufLen);
    if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, (PIP_ADAPTER_ADDRESSES)buf.data(), &outBufLen) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_ADDRESSES addr = (PIP_ADAPTER_ADDRESSES)buf.data(); addr != NULL; addr = addr->Next) {
            if (addr->OperStatus == IfOperStatusUp && addr->PhysicalAddressLength == 6) {
                char mac[18]; sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", addr->PhysicalAddress[0], addr->PhysicalAddress[1], addr->PhysicalAddress[2], addr->PhysicalAddress[3], addr->PhysicalAddress[4], addr->PhysicalAddress[5]);
                return std::string(mac);
            }
        }
    }
    return "UNKNOWN_HW_ID_0000";
}

bool SysUtils::ping_test(const std::string& ip, int targetSize) {
    int payloadSize = targetSize - 28; if (payloadSize < 0) payloadSize = 0;
    std::string cmd = "ping -n 1 -w 1000 -f -l " + std::to_string(payloadSize) + " " + ip;
    FILE* pipe = _popen(cmd.c_str(), "r"); if (!pipe) return false;
    char buffer[128]; std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) result += buffer;
    _pclose(pipe); std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    if (result.find("100% loss") != std::string::npos || result.find("fragmented") != std::string::npos || result.find("timeout") != std::string::npos) return false;
    return true;
}

int SysUtils::discover_best_huang_mtu(const std::string& target_ip) {
    ZHI_LOG_INFO("==========================================================================");
    ZHI_LOG_INFO("📡 KÍCH HOẠT HỆ THỐNG TRINH SÁT MTU ĐỘNG [HOÀNG-HEURISTIC-ALGORITHM]");
    ZHI_LOG_INFO("==========================================================================");
    if (ping_test(target_ip, 1500)) return 1500;
    int currentUpper = 1500, currentLower = 1000;
    while (true) {
        if (currentUpper - currentLower <= 1) return 1000;
        int distance = (currentUpper - currentLower) / 2; int mid = currentLower + distance;
        if (ping_test(target_ip, mid)) {
            int lastSuccess = mid;
            for (int val = lastSuccess + 100; val < currentUpper; val += 100) { if (ping_test(target_ip, val)) { lastSuccess = val; } else { currentUpper = val; break; } }
            for (int val = lastSuccess + 10; val < currentUpper; val += 10) { if (ping_test(target_ip, val)) { lastSuccess = val; } else { currentUpper = val; break; } }
            for (int val = lastSuccess + 1; val < currentUpper; val++) { if (ping_test(target_ip, val)) { lastSuccess = val; } else { break; } }
            ZHI_LOG_INFO("🏆 THUẬT TOÁN KẾT THÚC! ĐÃ TÌM RA ĐỈNH MTU TỐI ƯU: " + std::to_string(lastSuccess) + " bytes");
            return lastSuccess;
        } else { currentUpper = mid; }
    }
}

void SysUtils::start_network_monitor(std::function<void()> on_change_callback) {
    std::thread([on_change_callback]() {
        std::string last_lan, last_ts; auto_detect_ips(last_lan, last_ts);
        while(true) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            std::string cur_lan, cur_ts; auto_detect_ips(cur_lan, cur_ts);
            if (cur_lan != last_lan || cur_ts != last_ts) {
                ZHI_LOG_WARN("⚡ [ALERT][MONITOR] PHÁT HIỆN CARD MẠNG SYSTEM CÓ BIẾN ĐỘNG PHẦN CỨNG!");
                last_lan = cur_lan; last_ts = cur_ts;
                on_change_callback();
            }
        }
    }).detach();
}

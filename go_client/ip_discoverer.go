package main

import (
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
)

// IsWirelessInterface kiểm tra bản chất phần cứng ở tầng Kernel Sysfs
// Dù Netplan có đổi tên card thành bất kỳ chuỗi gì, Kernel vẫn mount thư mục 'wireless' hoặc 'phy80211'
func IsWirelessInterface(ifaceName string) bool {
	sysPath := filepath.Join("/sys/class/net", ifaceName, "wireless")
	phyPath := filepath.Join("/sys/class/net", ifaceName, "phy80211")

	_, errWireless := os.Stat(sysPath)
	_, errPhy := os.Stat(phyPath)

	// Nếu tồn tại 1 trong 2 thư mục này -> Kh khẳng định 100% là Card Wi-Fi
	return !os.IsNotExist(errWireless) || !os.IsNotExist(errPhy)
}

func AutoDetectClientIPs() (string, string, bool) {
	log.Println("[DEBUG] [ip_discoverer.go] -> Scanning network interfaces via Kernel Sysfs hardware inspection...")
	lanIp, tsIp := "", ""
	ethIp, wifiIp := "", ""

	ifaces, err := net.Interfaces()
	if err != nil {
		log.Printf("[ERROR] [ip_discoverer.go] -> Failed to query net interfaces.")
		return "", "", false
	}

	for _, iface := range ifaces {
		// Bỏ qua card down hoặc loopback / docker / bridge / veth
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 || 
		   strings.Contains(iface.Name, "docker") || strings.Contains(iface.Name, "veth") || strings.Contains(iface.Name, "br-") {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil { continue }

		for _, addr := range addrs {
			ipNet, ok := addr.(*net.IPNet)
			if !ok || ipNet.IP.IsLoopback() || ipNet.IP.To4() == nil { continue }

			ipStr := ipNet.IP.String()
			name := iface.Name

			// 1. Nhận diện Tailscale (Mạng ảo WAN)
			if strings.Contains(name, "tailscale") || name == "ts0" || name == "tailscale0" {
				tsIp = ipStr
				log.Printf("[MATCH-TS] 🎯 Tailscale IP: %s (Interface: %s)", tsIp, name)
			} else {
				// 2. Phân loại chuẩn phần cứng bằng Sysfs Kernel Inspection
				isWifi := IsWirelessInterface(name)
				if isWifi {
					if wifiIp == "" { wifiIp = ipStr }
					log.Printf("[RADAR-HARDWARE] 📡 Card Không Dây (Wi-Fi): [%s] ---> IP: %s", name, ipStr)
				} else {
					if ethIp == "" { ethIp = ipStr }
					log.Printf("[RADAR-HARDWARE] 🚀 Card Có Dây (Ethernet): [%s] ---> IP: %s", name, ipStr)
				}
			}
		}
	}

	// 🔥 THUẬT TOÁN ƯU TIÊN MẠNG DÂY: Ưu tiên Card Dây (Ethernet) > Card Không Dây (Wi-Fi)
	if ethIp != "" {
		lanIp = ethIp
		log.Printf("[ACTIVE-LAN] 🚀 Khóa mục tiêu IP Mạng Dây Ethernet: %s", lanIp)
	} else if wifiIp != "" {
		lanIp = wifiIp
		log.Printf("[ACTIVE-LAN] 📡 Khóa mục tiêu IP Mạng Không Dây Wi-Fi: %s", lanIp)
	}

	log.Printf("[RESULT] ---> Dynamic Active LAN IP: [%s] | Tailscale IP: [%s]", lanIp, tsIp)
	return lanIp, tsIp, lanIp != "" || tsIp != ""
}

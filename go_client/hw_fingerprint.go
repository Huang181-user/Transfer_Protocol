package main

import (
	"log"
	"net"
	"strings"
)

// Trích xuất địa chỉ MAC của card mạng vật lý để làm vân tay chống trộm Token
func GetHardwareFingerprint() string {
	ifaces, err := net.Interfaces()
	if err != nil {
		log.Println("[WARNING] [hw_fingerprint.go] Không thể quét card mạng. Trả về mã HW_ID mặc định.")
		return "UNKNOWN_HW_ID_0000"
	}

	for _, i := range ifaces {
		// Bỏ qua các card loopback, docker ảo, chỉ lấy card vật lý đang up
		if i.Flags&net.FlagUp != 0 && !strings.Contains(i.Name, "lo") && !strings.Contains(i.Name, "docker") {
			mac := i.HardwareAddr.String()
			if mac != "" {
				log.Printf("[SECURITY] [hw_fingerprint.go] Khóa mục tiêu vân tay phần cứng vật lý (MAC): [%s]", mac)
				return mac
			}
		}
	}
	return "UNKNOWN_HW_ID_0000"
}

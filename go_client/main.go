package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#cgo LDFLAGS: -L../build -lzhiauth_client_core -lstdc++ -lsodium -lpthread -lws2_32 -lcredui -liphlpapi
#include "win_auth.h"
*/
import "C"
import (
	"bufio"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

func main() {
	log.SetFlags(0)
	configPath := "config/config.json"
	sessionPath := "config/.session"
	if _, err := os.Stat(configPath); os.IsNotExist(err) {
		configPath = "../config/config.json"
		sessionPath = "../config/.session"
	}

	fmt.Println("==========================================================================")
	fmt.Printf("🚀 HUANG HYBRID GO-C++ CLIENT v6.0 - HYPER KCP SPEED BOOST\n")
	fmt.Println("==========================================================================")

	cfg, err := LoadClientConfig(configPath)
	if err != nil || cfg == nil {
		log.Fatalf("❌ [FATAL] Cannot read configuration file. Error: %v", err)
	}
	cfg.ClientLanIp, cfg.ClientTsIp, _ = AutoDetectClientIPs()
	
	// Xác định IP đang kết nối được (LAN hay Tailscale)
	activeIp := DiscoverBestRoute(cfg)

	// 🔥 KÍCH HOẠT RADAR MTU TRINH SÁT TRƯỚC KHI NỔ MÁY
	pingFunc := func(targetSize int) bool {
		// Ping ICMP cần trừ hao 28 bytes header (20 bytes IPv4 + 8 bytes ICMP)
		payloadSize := targetSize - 28
		if payloadSize < 0 { payloadSize = 0 }
		
		// Gọi lệnh ping của Windows (-f: Không phân mảnh, -l: Kích thước payload)
		out, err := exec.Command("ping", "-n", "1", "-w", "1000", "-f", "-l", strconv.Itoa(payloadSize), activeIp).CombinedOutput()
		outStr := strings.ToLower(string(out))
		
		// Phân tích kỹ output của lệnh Ping trên Windows (Bắt lỗi phân mảnh hoặc RTO)
		if err != nil || strings.Contains(outStr, "100% loss") || strings.Contains(outStr, "fragmented") || strings.Contains(outStr, "phân mảnh") || strings.Contains(outStr, "timeout") {
			return false
		}
		return true
	}
	
	// Ép ngược MTU động tìm được vào hệ thống thay cho số tĩnh trong config.json
	optimalMTU := DiscoverBestHuangMTU(pingFunc)
	cfg.KcpMtu = optimalMTU 

	user, pass, _, exists := LoadDeviceSession(sessionPath)
	if !exists {
		creds := GetUserCredentials()
		user = creds.Username
		pass = creds.Password
		// Gỡ cứng chữ A:, không lưu ổ đĩa xuống Credential vì nó ở trên JSON rồi
		SaveDeviceSession(sessionPath, user, pass, "")
	}

	hwID := GetHardwareFingerprint()
	InitClientID(hwID) // 🔥 Gắn cứng HWID thành Client_ID

	authCmd := fmt.Sprintf("AUTH_REQ|USER:%s|PASS:%s|LAN:%s|TS:%s|HWID:%s", user, pass, cfg.ClientLanIp, cfg.ClientTsIp, hwID)

	// Lấy cổng, ổ đĩa và Conv động 100% từ file config.json
	tunnel := NewQuicTunnel(activeIp, cfg.AuthPort, cfg.ServerPort, authCmd, cfg.MountKcpDrive, cfg.MountQuicDrive)
	if err := tunnel.ReconnectSilently(); err != nil {
		log.Fatalf("❌ [ACCESS_DENIED] Authentication failed from Server: %v", err)
	}

	// 🔥 LẤY CỔNG KCP ĐỘNG TỪ SERVER (Chống Server đổi cổng mà Client không biết)
	if tunnel.DynamicKcpPort > 0 {
		cfg.KcpPort = tunnel.DynamicKcpPort
	}

	log.Printf("[%s] [CGO-INIT] Đánh thức C++ KCP Engine (Port: %d, MTU TỐI ƯU: %d, CONV ID: %d)...", time.Now().Format("2006-01-02 15:04:05.000"), cfg.KcpPort, cfg.KcpMtu, cfg.KcpConv)
	if !InitCppSDK(activeIp, cfg.KcpPort, cfg.KcpKey, cfg.KcpMtu, cfg.KcpConv) {
		log.Fatal("❌ [FATAL] C++ Core Engine failed to initialize!")
	}
	defer ShutdownCppSDK()

	// Truyền ổ đĩa động từ JSON vào DualFuse
	StartDualFuseSubsystem(cfg.MountKcpDrive, cfg.MountQuicDrive, tunnel.AssignedPath, tunnel)

	// Heartbeat QUIC
	go func() {
		ticker := time.NewTicker(10 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			go func() {
				pingPacket := BuildVfsPacket(OP_PING, "/", 0, 0, nil)
				quicPayload := append([]byte("FS_CMD|"), pingPacket...)
				tunnel.SendFsCommandRaw(quicPayload)
			}()
		}
	}()

	// Heartbeat KCP
	go func() {
		ticker := time.NewTicker(15 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			go func() {
				SendRpcVfs(OP_PING, "/", 0, 0, nil)
			}()
		}
	}()

	fmt.Println("\n✅ Daemon running in background at MAXIMUM SPEED. Type 'exit' to quit, 'logout' to sign out.")

	// Unmount động theo config json
	cleanupNetworkDrives := func() {
		log.Printf("[SHUTDOWN] Đang tháo nóng hai ổ đĩa %s: và %s:...", cfg.MountKcpDrive, cfg.MountQuicDrive)
		exec.Command("net", "use", cfg.MountKcpDrive+":", "/delete", "/y").Run()
		exec.Command("net", "use", cfg.MountQuicDrive+":", "/delete", "/y").Run()
		log.Println("[SHUTDOWN] ✅ Network drives unmounted cleanly.")
	}

	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		cmd := strings.TrimSpace(strings.ToLower(scanner.Text()))
		if cmd == "exit" || cmd == "quit" {
			log.Println("\n[EXIT] Shutting down client safely...")
			cleanupNetworkDrives()
			os.Exit(0)
		}
		if cmd == "logout" {
			log.Println("\n[LOGOUT] Logging out...")
			ClearDeviceSession(sessionPath)
			cleanupNetworkDrives()
			os.Exit(0)
		}
	}

	select {}
}
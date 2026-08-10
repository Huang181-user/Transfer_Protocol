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
	"io"
	"log"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// =========================================================================
// 🔥 BỘ LỌC TÀNG HÌNH: CHE IP & ĐƯỜNG DẪN TỰ ĐỘNG CHO TOÀN BỘ LOG GOLANG
// =========================================================================
type MaskedLogWriter struct {
	Out io.Writer
}

func (m *MaskedLogWriter) Write(p []byte) (int, error) {
	msg := string(p)

	// 1. Che IP LAN & Tailscale (VD: 100.125.141.48 -> 100.***.***.48)
	ipRe := regexp.MustCompile(`\b(\d{1,3})\.\d{1,3}\.\d{1,3}\.(\d{1,3})\b`)
	msg = ipRe.ReplaceAllString(msg, "$1.***.***.$2")

	// 2. Che đường dẫn VFS nhưng giữ lại tên file để dễ Debug
	// VD: /export/HDD_merge/Secret.txt -> /***/***/Secret.txt
	pathRe := regexp.MustCompile(`(/[^\s"',:;]+)+/([^\s"',:;]+)`)
	msg = pathRe.ReplaceAllStringFunc(msg, func(match string) string {
		parts := strings.Split(match, "/")
		if len(parts) > 0 {
			return "/***/***/" + parts[len(parts)-1]
		}
		return match
	})

	return m.Out.Write([]byte(msg))
}

func main() {
	log.SetFlags(0)
	// Kích hoạt bộ lọc log toàn cục cho Golang
	log.SetOutput(&MaskedLogWriter{Out: os.Stdout})

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
	
	activeIp := DiscoverBestRoute(cfg)

	// KÍCH HOẠT RADAR MTU
	pingFunc := func(targetSize int) bool {
		payloadSize := targetSize - 28
		if payloadSize < 0 { payloadSize = 0 }
		out, err := exec.Command("ping", "-n", "1", "-w", "1000", "-f", "-l", strconv.Itoa(payloadSize), activeIp).CombinedOutput()
		outStr := strings.ToLower(string(out))
		if err != nil || strings.Contains(outStr, "100% loss") || strings.Contains(outStr, "fragmented") || strings.Contains(outStr, "phân mảnh") || strings.Contains(outStr, "timeout") {
			return false
		}
		return true
	}
	
	optimalMTU := DiscoverBestHuangMTU(pingFunc)
	cfg.KcpMtu = optimalMTU - 28

	user, pass, _, exists := LoadDeviceSession(sessionPath)
	if !exists {
		creds := GetUserCredentials()
		user = creds.Username
		pass = creds.Password
		SaveDeviceSession(sessionPath, user, pass, "")
	}

	hwID := GetHardwareFingerprint()
	InitClientID(hwID) 

	authCmd := fmt.Sprintf("AUTH_REQ|USER:%s|PASS:%s|LAN:%s|TS:%s|HWID:%s", user, pass, cfg.ClientLanIp, cfg.ClientTsIp, hwID)

	tunnel := NewQuicTunnel(activeIp, cfg.AuthPort, cfg.ServerPort, authCmd, cfg.MountKcpDrive, cfg.MountQuicDrive)
	if err := tunnel.ReconnectSilently(); err != nil {
		log.Fatalf("❌ [ACCESS_DENIED] Authentication failed from Server: %v", err)
	}

	if tunnel.DynamicKcpPort > 0 {
		cfg.KcpPort = tunnel.DynamicKcpPort
	}

	log.Printf("[%s] [CGO-INIT] Đánh thức C++ KCP Engine (Port: %d, MTU: %d, CONV: %d)...", time.Now().Format("2006-01-02 15:04:05.000"), cfg.KcpPort, cfg.KcpMtu, cfg.KcpConv)

	// KHÔNG ĐỤNG CHẠM ĐẾN KCP TUNING CẤP PHÁT ĐỘNG!
	if !InitCppSDK(activeIp, cfg.KcpPort, cfg.KcpKey, cfg.KcpMtu, cfg.KcpConv, 
		tunnel.KcpNoDelay, tunnel.KcpInterval, tunnel.KcpResend, tunnel.KcpNc, tunnel.KcpSndWnd, tunnel.KcpRcvWnd) {
		log.Fatal("❌ [FATAL] C++ Core Engine failed to initialize!")
	}
	defer ShutdownCppSDK()

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
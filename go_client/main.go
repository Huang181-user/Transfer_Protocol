package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

func main() {
	log.SetFlags(0)
	ts := time.Now().Format("2006-01-02 15:04:05.000000")
	sessionPath := "../config/.session"
	if _, err := os.Stat("config"); err == nil {
		sessionPath = "config/.session"
	}

	fmt.Println("==========================================================================")
	fmt.Printf("🚀 HUANG HYBRID GO-C++ CLIENT v6.0 - DUAL-MOUNT KCP & QUIC [%s]\n", ts)
	fmt.Println("==========================================================================")

	cfg, _ := LoadClientConfig("../config/config.json")
	user, pass, mountPath, exists := LoadDeviceSession(sessionPath)

	if !exists {
		creds := GetUserCredentials()
		user = creds.Username
		pass = creds.Password
		mountPath = creds.MountPoint
	}

	cfg.ClientLanIp, cfg.ClientTsIp, _ = AutoDetectClientIPs()
	activeIp := DiscoverBestRoute(cfg)
	if activeIp == "" {
		log.Fatalf("[%s] [FATAL] Server biệt tích!", time.Now().Format("2006-01-02 15:04:05.000"))
	}

	pingFunc := func(targetSize int) bool {
		payloadSize := targetSize - 28
		if payloadSize < 0 {
			payloadSize = 0
		}
		cmd := exec.Command("ping", "-c", "1", "-W", "1", "-M", "do", "-s", fmt.Sprintf("%d", payloadSize), activeIp)
		return cmd.Run() == nil
	}
	bestMtu := DiscoverBestHuangMTU(pingFunc)
	log.Printf("[%s] [MTU-LOCKED] Chốt sổ cấu hình MTU toàn hệ thống: %d bytes", time.Now().Format("2006-01-02 15:04:05.000"), bestMtu)

	hwID := GetHardwareFingerprint()
	authCmd := fmt.Sprintf("AUTH_REQ|USER:%s|PASS:%s|LAN:%s|TS:%s|HWID:%s", user, pass, cfg.ClientLanIp, cfg.ClientTsIp, hwID)

	// KHỞI TẠO ĐƯỜNG HẦM (Bỏ tham số Port tĩnh)
	tunnel := NewQuicTunnel(activeIp, cfg.AuthPort, authCmd)
	err := tunnel.ReconnectSilently()
	if err != nil {
		os.Remove(sessionPath)
		log.Fatalf("[%s] ❌ [ACCESS_DENIED] Xác thực sập: %v", time.Now().Format("2006-01-02 15:04:05.000"), err)
	}

	// Kích nổ luồng nhịp tim
	go tunnel.StartHeartbeat()

	if !exists {
		SaveDeviceSession(sessionPath, user, pass, mountPath)
	}

	log.Printf("[%s] [CGO-INIT] Đang kích hoạt động cơ C++ KCP...", time.Now().Format("2006-01-02 15:04:05.000000"))

	// 🔥 TRÍCH XUẤT PORT KCP VỪA NHẬN TỪ SERVER ĐỂ NẠP CHO LÕI C++
	kcpPortInt, _ := strconv.Atoi(tunnel.AssignedKcpPort)
	if !InitCppSDK(activeIp, kcpPortInt, cfg.MasterSymKey, bestMtu) {
		log.Fatalf("[%s] [FATAL] Lõi C++ từ chối khởi động!", time.Now().Format("2006-01-02 15:04:05.000"))
	}

	vfsPath := filepath.Join(mountPath, "VFS_DRIVE")
	quicPath := filepath.Join(mountPath, "QUIC_DRIVE")
	exec.Command("sudo", "fusermount", "-uz", vfsPath).Run()
	exec.Command("sudo", "fusermount", "-uz", quicPath).Run()
	exec.Command("sudo", "umount", "-l", vfsPath).Run()
	exec.Command("sudo", "umount", "-l", quicPath).Run()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		log.Printf("\n[%s] [SHUTDOWN] Đang dọn dẹp hệ thống. Tháo đĩa an toàn...", time.Now().Format("2006-01-02 15:04:05.000"))
		ShutdownCppSDK()
		exec.Command("sudo", "fusermount", "-uz", vfsPath).Run()
		exec.Command("sudo", "fusermount", "-uz", quicPath).Run()
		os.Exit(0)
	}()

	log.Printf("[%s] [SYSTEM-READY] Trục kép VFS đã sẵn sàng. Tiến hành nổ máy Dual-Mount!", time.Now().Format("2006-01-02 15:04:05.000000"))
	StartDualFuseSubsystem(mountPath, tunnel.AssignedPath, tunnel)

	fmt.Println("\n✅ Hệ thống đã chạy ngầm. Gõ 'logout' hoặc 'exit' để đăng xuất an toàn.")
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		cmd := strings.TrimSpace(strings.ToLower(scanner.Text()))
		if cmd == "logout" || cmd == "exit" {
			log.Printf("\n[%s] [LOGOUT] Đang tiến hành đăng xuất và xóa thẻ bài Token...", time.Now().Format("2006-01-02 15:04:05.000"))
			os.Remove(sessionPath)
			sigChan <- syscall.SIGTERM
			break
		}
	}

	select {}
}

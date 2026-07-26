package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"strings"
)

// UserCredentials đóng gói dữ liệu nhập liệu của hệ thống
type UserCredentials struct {
	Username   string
	Password   string
	MountPoint string
}

// GetUserCredentials thực thi chặn luồng stdin để lấy dữ liệu trước khi mở cổng mạng
func GetUserCredentials() UserCredentials {
	log.Println("[DEBUG][CRED-LOAD] -> Khởi động luồng thu thập thông tin xác thực tương tác...")

	reader := bufio.NewReader(os.Stdin)
	creds := UserCredentials{}

	// 1. Thu thập Username
	fmt.Print("👤 Nhập tài khoản hệ thống (Username): ")
	username, _ := reader.ReadString('\n')
	creds.Username = strings.TrimSpace(username)
	log.Printf("[DEBUG][INPUT-EVAL] -> Ghi nhận trường [Username] có độ dài chuỗi: %d ký tự", len(creds.Username))

	// 2. Thu thập Password
	fmt.Print("🔑 Nhập mật khẩu bảo mật (Password): ")
	password, _ := reader.ReadString('\n')
	creds.Password = strings.TrimSpace(password)
	// Log độ dài để debug nhưng che nội dung (Masked) để bảo mật
	log.Printf("[DEBUG][INPUT-EVAL] -> Ghi nhận trường [Password] có độ dài chuỗi: %d ký tự [MASKED_FOR_SECURITY]", len(creds.Password))

	// 3. Thu thập Vị trí gán ổ đĩa (Mount Point)
	fmt.Print("👉 Vị trí muốn gán ổ đĩa trên máy Client (Mặc định: /mnt/Cloud): ")
	mountPoint, _ := reader.ReadString('\n')
	mountPoint = strings.TrimSpace(mountPoint)
	if mountPoint == "" {
		creds.MountPoint = "/mnt/Cloud"
	} else {
		creds.MountPoint = mountPoint
	}
	log.Printf("[DEBUG][INPUT-EVAL] -> Target Directory Allocated: [%s]", creds.MountPoint)
	log.Println("[SUCCESS][CRED-LOAD] -> Thông tin xác thực đã được liên kết thành công vào bộ nhớ.")

	return creds
}

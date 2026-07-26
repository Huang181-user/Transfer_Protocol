package main

import (
	"encoding/json"
	"log"
	"os"
)

type AppConfig struct {
	Network struct {
		AuthPort     int `json:"auth_port"`
		QuicDataPort int `json:"quic_data_port"`
		KcpDataPort  int `json:"kcp_data_port"`
		CustomMtu    int `json:"custom_mtu"`
	} `json:"network"`
	Paths struct {
		SafeRoot string `json:"safe_root"`
		LogPath  string `json:"log_path"`
		TlsCrt   string `json:"tls_crt"`
		TlsKey   string `json:"tls_key"`
		Database string `json:"database"`
	} `json:"paths"`
	Security struct {
		MasterSymKey       string `json:"master_sym_key"`
		HashSalt           string `json:"hash_salt"`
		SystemAdminUser    string `json:"system_admin_user"`
		MaxFailAttempts    int    `json:"max_fail_attempts"`
		BanDurationMinutes int    `json:"ban_duration_minutes"`
	} `json:"security"`
}

var globalConfig AppConfig

func LoadConfig(path string) {
	file, err := os.Open(path)
	if err != nil {
		log.Fatalf("[FATAL] Không thể mở file config.json: %v", err)
	}
	defer file.Close()

	if err := json.NewDecoder(file).Decode(&globalConfig); err != nil {
		log.Fatalf("[FATAL] File config.json sai định dạng: %v", err)
	}
	log.Println("[SYSTEM] Đã tải cấu hình bảo mật từ config.json thành công!")
}

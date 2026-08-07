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
	KcpTuning struct {
		NoDelay  int `json:"nodelay"`
		Interval int `json:"interval"`
		Resend   int `json:"resend"`
		Nc       int `json:"nc"`
		SndWnd   int `json:"snd_wnd"`
		RcvWnd   int `json:"rcv_wnd"`
	} `json:"kcp_tuning"`
	Paths struct {
		SafeRoot string `json:"safe_root"`
		LogPath  string `json:"log_path"`
		Database string `json:"database"`
	} `json:"paths"`
	Security struct {
		MasterSymKey    string `json:"master_sym_key"`
		HashSalt        string `json:"hash_salt"`
		SystemAdminUser string `json:"system_admin_user"`
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
	log.Println("[SYSTEM] Đã nạp cấu hình KCP Dynamic Tuning từ config.json thành công!")
}
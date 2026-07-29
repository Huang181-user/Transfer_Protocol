package main

import (
	"encoding/json"
	"log"
	"os"
	"time"
)

type ClientConfig struct {
	ServerPort      string `json:"server_port"`
	AuthPort        string `json:"auth_port"`
	KcpPort         int    `json:"kcp_port"`
	LocalPort       string `json:"local_port"`
	ServerLanIp     string `json:"server_lan_ip"`
	ServerTsIp      string `json:"server_ts_ip"`
	KcpKey          string `json:"kcp_key"`
	KcpMtu          int    `json:"kcp_mtu"`
	KcpConv         uint32 `json:"kcp_conv"`
	MountKcpDrive   string `json:"mount_kcp_drive"`
	MountQuicDrive  string `json:"mount_quic_drive"`
	ClientLanIp     string
	ClientTsIp      string
}

func LoadClientConfig(filepath string) (*ClientConfig, error) {
	ts := time.Now().Format("2006-01-02 15:04:05.000")
	log.Printf("[%s] [CONFIG] Đang kéo cấu hình nhạy cảm từ JSON: %s", ts, filepath)

	file, err := os.Open(filepath)
	if err != nil {
		log.Printf("[%s] [CONFIG-ERROR] KHÔNG TÌM THẤY CONFIG ở %s: %v", ts, filepath, err)
		return nil, err
	}
	defer file.Close()

	var cfg ClientConfig
	if err := json.NewDecoder(file).Decode(&cfg); err != nil {
		log.Printf("[%s] [CONFIG-ERROR] File cấu hình JSON bị hỏng: %v", ts, err)
		return nil, err
	}

	// Chặn mọi điểm mù thiếu config
	if cfg.AuthPort == "" { cfg.AuthPort = "5555" }
	if cfg.ServerPort == "" { cfg.ServerPort = "4433" }
	if cfg.KcpPort == 0 { cfg.KcpPort = 6666 }
	if cfg.KcpMtu == 0 { cfg.KcpMtu = 1350 }
	if cfg.KcpConv == 0 { cfg.KcpConv = 0x11223344 }
	if cfg.MountKcpDrive == "" { cfg.MountKcpDrive = "X" }
	if cfg.MountQuicDrive == "" { cfg.MountQuicDrive = "Y" }

	log.Printf("[%s] [CONFIG-SUCCESS] Nạp Config Hoàn Tất:", ts)
	log.Printf(" ├── [NET] Data/Auth Port  : %s / %s", cfg.ServerPort, cfg.AuthPort)
	log.Printf(" ├── [KCP] Port/MTU/CONV   : %d / %d / %d", cfg.KcpPort, cfg.KcpMtu, cfg.KcpConv)
	log.Printf(" ├── [MOUNT] Drives        : %s: (KCP) | %s: (QUIC)", cfg.MountKcpDrive, cfg.MountQuicDrive)
	log.Printf(" └── [KEY] Mật mã KCP      : [ĐÃ MÃ HÓA BẢO VỆ / %d bytes]", len(cfg.KcpKey))

	return &cfg, nil
}
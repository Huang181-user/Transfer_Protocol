package main

import (
	"encoding/json"
	"log"
	"os"
	"time"
)

type ClientConfig struct {
	AuthPort     string `json:"auth_port"`
	LocalPort    string `json:"local_port"`
	ServerLanIp  string `json:"server_lan_ip"`
	ServerTsIp   string `json:"server_ts_ip"`
	SniDomain    string `json:"sni_domain"`
	CustomMtu    int    `json:"custom_mtu"`
	MasterSymKey string `json:"master_sym_key"`
	ClientLanIp  string
	ClientTsIp   string
}

func LoadClientConfig(filepath string) (*ClientConfig, error) {
	log.Printf("[%s] [DEBUG] [config_handler.go:LoadClientConfig] -> Attempting to parse descriptor node on disk: %s", time.Now().Format("2006-01-02 15:04:05.000"), filepath)
	file, err := os.Open(filepath)
	if err != nil {
		log.Printf("[%s] [ERROR] [config_handler.go:LoadClientConfig] -> Target json matrix configuration unreadable.", time.Now().Format("2006-01-02 15:04:05.000"))
		return nil, err
	}
	defer file.Close()

	var cfg ClientConfig
	decoder := json.NewDecoder(file)
	if err := decoder.Decode(&cfg); err != nil {
		return nil, err
	}

	log.Printf("[%s] [SUCCESS] [config_handler.go:LoadClientConfig] -> Struct aligned. Target Server -> LAN: %s | TS: %s | Auth Port: %s", time.Now().Format("2006-01-02 15:04:05.000"), cfg.ServerLanIp, cfg.ServerTsIp, cfg.AuthPort)
	return &cfg, nil
}

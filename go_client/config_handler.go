package main

import (
	"encoding/json"
	"log"
	"os"
)

type ClientConfig struct {
	ServerPort   string `json:"server_port"`
	AuthPort     string `json:"auth_port"`
	KcpPort      string `json:"kcp_port"`
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
	log.Printf("[DEBUG] [config_handler.go:LoadClientConfig] -> Attempting to parse descriptor node on disk: %s", filepath)
	file, err := os.Open(filepath)
	if err != nil {
		log.Printf("[ERROR] [config_handler.go:LoadClientConfig] -> Target json matrix configuration unreadable.")
		return nil, err
	}
	defer file.Close()

	var cfg ClientConfig
	decoder := json.NewDecoder(file)
	if err := decoder.Decode(&cfg); err != nil {
		return nil, err
	}

	log.Printf("[SUCCESS] [config_handler.go:LoadClientConfig] -> Struct aligned. Target Server -> LAN: %s | TS: %s | QUIC Data Port: %s", cfg.ServerLanIp, cfg.ServerTsIp, cfg.ServerPort)
	return &cfg, nil
}

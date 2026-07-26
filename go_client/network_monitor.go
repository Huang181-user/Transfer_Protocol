package main

import (
	"log"
	"net"
	"time"
)

type NetworkMonitor struct {
	isRunning bool
}

func NewNetworkMonitor() *NetworkMonitor { return &NetworkMonitor{isRunning: false} }

func (m *NetworkMonitor) GetCurrentLinuxIPs() []string {
	var ips []string
	ifaces, err := net.Interfaces()
	if err != nil { return ips }
	for _, iface := range ifaces {
		addrs, err := iface.Addrs()
		if err != nil { continue }
		for _, addr := range addrs {
			ipNet, ok := addr.(*net.IPNet)
			if !ok || ipNet.IP.IsLoopback() || ipNet.IP.To4() == nil { continue }
			ips = append(ips, ipNet.IP.String())
		}
	}
	return ips
}

func (m *NetworkMonitor) StartMonitoring(onChangeCallback func()) {
	m.isRunning = true
	log.Println("[DEBUG] [network_monitor.go] 👁️ Khởi động Mắt thần canh gác Card mạng Linux Engine (Chu kỳ quét: 3s)...")

	go func() {
		lastIps := m.GetCurrentLinuxIPs()
		for m.isRunning {
			time.Sleep(3 * time.Second)
			currentIps := m.GetCurrentLinuxIPs()

			if !equalSlices(lastIps, currentIps) {
				log.Println("\n==========================================================================")
				log.Println("[⚡ ALERT][MONITOR] PHÁT HIỆN CARD MẠNG SYSTEM LINUX CÓ BIẾN ĐỘNG PHẦN CỨNG!")
				log.Println("==========================================================================")
				lastIps = currentIps
				onChangeCallback()
			}
		}
	}()
}

func (m *NetworkMonitor) StopMonitoring() { m.isRunning = false }

func equalSlices(a, b []string) bool {
	if len(a) != len(b) { return false }
	for i := range a {
		if a[i] != b[i] { return false }
	}
	return true
}

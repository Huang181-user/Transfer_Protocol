package main
import ("net"; "strings")
func AutoDetectClientIPs() (string, string, bool) {
    lanIp, tsIp := "", ""
    ifaces, _ := net.Interfaces()
    for _, iface := range ifaces {
        addrs, _ := iface.Addrs()
        for _, addr := range addrs {
            ipNet, ok := addr.(*net.IPNet); if !ok || ipNet.IP.IsLoopback() || ipNet.IP.To4() == nil { continue }
            ipStr := ipNet.IP.String(); name := iface.Name
            if strings.Contains(name, "tailscale") || name == "ts0" || name == "Tailscale" { tsIp = ipStr } else if strings.HasPrefix(name, "en") || strings.HasPrefix(name, "wl") || strings.HasPrefix(name, "eth") || strings.Contains(name, "Ethernet") || strings.Contains(name, "Wi-Fi") { lanIp = ipStr }
        }
    }
    return lanIp, tsIp, lanIp != "" || tsIp != ""
}

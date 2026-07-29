package main
import ("net"; "strings")
func GetHardwareFingerprint() string {
    ifaces, err := net.Interfaces(); if err != nil { return "UNKNOWN_HW_ID_0000" }
    for _, i := range ifaces {
        if i.Flags&net.FlagUp != 0 && !strings.Contains(i.Name, "lo") && !strings.Contains(i.Name, "docker") {
            mac := i.HardwareAddr.String(); if mac != "" { return mac }
        }
    }
    return "UNKNOWN_HW_ID_0000"
}

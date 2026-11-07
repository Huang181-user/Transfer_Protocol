import SwiftUI
import UIKit
import Darwin
import Network

func getDeviceIPs() -> (lan: String, ts: String) {
    var lan = ""; var ts = ""
    var ifaddr: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&ifaddr) == 0 else { return (lan, ts) }
    var ptr = ifaddr
    while ptr != nil {
        defer { ptr = ptr?.pointee.ifa_next }
        guard let interface = ptr?.pointee else { continue }
        let addrFamily = interface.ifa_addr.pointee.sa_family
        if addrFamily == UInt8(AF_INET) {
            let name = String(cString: interface.ifa_name)
            var hostname = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            getnameinfo(interface.ifa_addr, socklen_t(interface.ifa_addr.pointee.sa_len), &hostname, socklen_t(hostname.count), nil, socklen_t(0), NI_NUMERICHOST)
            let ip = String(cString: hostname)
            if ip.hasPrefix("127.") || ip.hasPrefix("169.254.") { continue }
            if name == "en0" || name == "pdp_ip0" { lan = ip } else if name.hasPrefix("utun") && ip.hasPrefix("100.") { ts = ip }
        }
    }
    freeifaddrs(ifaddr)
    return (lan, ts)
}

struct LoginView: View {
    @EnvironmentObject var state: AppState
    @AppStorage("savedLanIP") private var lanIP: String = AppConfig.shared.defaultLanIp
    @AppStorage("savedTsIP") private var tsIP: String = AppConfig.shared.defaultTsIp
    @AppStorage("savedUser") private var username: String = AppConfig.shared.defaultUsername
    @AppStorage("savedPass") private var password: String = ""
    
    var body: some View {
        VStack(spacing: 14) {
            Text("ZhiAuth v6.0").font(.largeTitle).bold().padding(.top, 20)
            Text("Dual-Tunnel: KCP (Primary) + QUIC (Fallback)")
                .font(.caption).foregroundColor(.secondary)
            
            VStack(spacing: 10) {
                TextField("LAN IP Server", text: $lanIP)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numbersAndPunctuation)
                    .autocapitalization(.none)
                TextField("Tailscale IP Server", text: $tsIP)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numbersAndPunctuation)
                    .autocapitalization(.none)
                TextField("Username", text: $username)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .autocapitalization(.none)
                SecureField("Password", text: $password)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
            }
            .padding(.horizontal, 24)
            
            if state.isLoading {
                ProgressView("Đang xác thực & mở song song KCP + QUIC...").padding(.vertical, 4)
            }
            
            Button(action: performLogin) {
                Text("ĐĂNG NHẬP HỆ THỐNG")
                    .font(.headline).foregroundColor(.white).padding()
                    .frame(maxWidth: .infinity)
                    .background(state.isLoading ? Color.gray : Color.blue)
                    .cornerRadius(10)
            }
            .disabled(state.isLoading)
            .padding(.horizontal, 24)
            
            ScrollView {
                Text(state.logs)
                    .font(.system(size: 10, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(10)
            }
            .background(Color.black.opacity(0.88))
            .foregroundColor(.green)
            .cornerRadius(10)
            .padding(.horizontal, 16)
            .padding(.bottom, 12)
        }
    }
    
    private func performLogin() {
        let safeLanIP = lanIP.trimmingCharacters(in: .whitespacesAndNewlines)
        let safeTsIP = tsIP.trimmingCharacters(in: .whitespacesAndNewlines)
        let activeSymKey = AppConfig.shared.kcpSymmetricKey
        
        if safeLanIP.isEmpty && safeTsIP.isEmpty {
            state.log("❌ Lỗi: Phải nhập ít nhất 1 địa chỉ IP Server!")
            return
        }
        if activeSymKey.count != 32 {
            state.log("❌ Lỗi: KCP Master Key trong ZhiConfig.json chưa đúng 32 ký tự!")
            return
        }
        
        state.isLoading = true
        state.log("🚀 Bắt đầu Port Knocking (Đua tốc độ LAN & Tailscale)...")
        
        Task {
            do {
                let hwid = await MainActor.run { UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS" }
                let myIPs = getDeviceIPs()
                let lanStr = myIPs.lan.isEmpty ? "N/A" : myIPs.lan
                let tsStr = myIPs.ts.isEmpty ? "N/A" : myIPs.ts
                let authCmd = "AUTH_REQ|USER:\(username)|PASS:\(password)|LAN:\(lanStr)|TS:\(tsStr)|HWID:\(hwid)"
                
                let winner: (String, AuthResponse)? = await withTaskGroup(of: Optional<(String, AuthResponse)>.self) { group in
                    if !safeLanIP.isEmpty {
                        group.addTask {
                            do { return (safeLanIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeLanIP, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd)) }
                            catch { return nil }
                        }
                    }
                    if !safeTsIP.isEmpty {
                        group.addTask {
                            do { return (safeTsIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeTsIP, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd)) }
                            catch { return nil }
                        }
                    }
                    for await result in group {
                        if let res = result, res.1.isSuccess {
                            group.cancelAll()
                            return res
                        }
                    }
                    return nil
                }
                
                guard let (winningIP, auth) = winner else {
                    state.log("❌ Xác thực thất bại hoặc Timeout cả LAN & Tailscale!")
                    await MainActor.run { 
                        state.isLoading = false 
                    }
                    return
                }
                
                state.log("✅ Auth OK qua [\(winningIP)] | KCP Port: \(auth.kcpPort) | QUIC Port: \(auth.quicPort)")
                
                let kcpOk = ZhiKcpEngine.initCore(
                    ip: winningIP,
                    port: Int32(auth.kcpPort),
                    symKey: activeSymKey,
                    mtu: AppConfig.shared.defaultMtu,
                    tuning: auth.tuning
                )
                state.quicTunnel.configure(ip: winningIP, port: UInt16(auth.quicPort))
                
                if kcpOk {
                    state.log("🔥 Lõi C++ KCP Engine đã nổ máy thành công!")
                } else {
                    state.log("⚠️ Lõi KCP gặp sự cố, hệ thống vẫn có luồng QUIC sẵn sàng!")
                }
                
                try await Task.sleep(nanoseconds: 400_000_000)
                
                await MainActor.run {
                    state.sharedRoot = auth.sharedPath.isEmpty ? "/tmp" : auth.sharedPath
                    state.sessionToken = authCmd.components(separatedBy: "|").first(where: { $0.hasPrefix("TOKEN:") })?.replacingOccurrences(of: "TOKEN:", with: "") ?? ""
                    state.baseIP = winningIP
                    state.kcpPort = auth.kcpPort
                    state.quicPort = UInt16(auth.quicPort)
                    state.currentPath = "/"
                    state.activeTunnel = .none
                    state.isLoggedIn = true
                    state.isLoading = false
                    state.startWatchdog()
                }
            } catch {
                state.log("❌ Lỗi đăng nhập: \(error.localizedDescription)")
                await MainActor.run { 
                    state.isLoading = false 
                }
            }
        }
    }
}

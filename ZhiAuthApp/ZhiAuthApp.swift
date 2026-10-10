import SwiftUI
import UIKit
import Darwin

func getDeviceIPs() -> (lan: String, ts: String) {
    var lan = ""
    var ts = ""
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
            getnameinfo(interface.ifa_addr, socklen_t(interface.ifa_addr.pointee.sa_len),
                        &hostname, socklen_t(hostname.count),
                        nil, socklen_t(0), NI_NUMERICHOST)
            let ip = String(cString: hostname)
            
            if ip.hasPrefix("127.") || ip.hasPrefix("169.254.") { continue }
            if name == "en0" || name == "pdp_ip0" { lan = ip } 
            else if name.hasPrefix("utun") && ip.hasPrefix("100.") { ts = ip }
        }
    }
    freeifaddrs(ifaddr)
    return (lan, ts)
}

@main
struct ZhiAuthApp: App {
    @AppStorage("savedLanIP") private var lanIP: String = "192.168.1.83"
    @AppStorage("savedTsIP") private var tsIP: String = ""
    @AppStorage("savedUser") private var username: String = "huang"
    @AppStorage("savedPass") private var password: String = ""
    
    @State private var logText: String = "💀 Sẵn sàng chọc thủng server...\n"
    @State private var isRunning = false

    var body: some Scene {
        WindowGroup {
            VStack(spacing: 15) {
                Text("ZhiAuth v6.0")
                    .font(.largeTitle).bold()
                
                Text(isRunning ? "🚀 Engine đang gầm rú..." : "🛑 Đang ngủ")
                    .foregroundColor(isRunning ? .green : .red)
                
                VStack(spacing: 10) {
                    TextField("LAN IP Server", text: $lanIP)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                        .keyboardType(.decimalPad)
                    
                    TextField("Tailscale IP Server", text: $tsIP)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                        .keyboardType(.decimalPad)
                    
                    TextField("Username", text: $username)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                        .autocapitalization(.none)
                    
                    SecureField("Password", text: $password)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                }
                .padding(.horizontal)
                
                ScrollView {
                    Text(logText)
                        .font(.system(.caption, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding()
                }
                .background(Color.black.opacity(0.85))
                .foregroundColor(.green)
                .cornerRadius(10)
                .padding(.horizontal)
                
                Button(action: { testKcpCore() }) {
                    Text("KÍCH NỔ LÕI C++ KCP")
                        .font(.headline)
                        .foregroundColor(.white)
                        .padding()
                        .frame(maxWidth: .infinity)
                        .background(isRunning ? Color.gray : Color.blue)
                        .cornerRadius(10)
                }
                .disabled(isRunning)
                .padding()
            }
        }
    }
    
    func testKcpCore() {
        let targetIP = !lanIP.isEmpty ? lanIP : tsIP
        if targetIP.isEmpty { appendLog("❌ Lỗi: Phải nhập ít nhất 1 IP Server!"); return }
        if username.isEmpty || password.isEmpty { appendLog("❌ Lỗi: Username và Password không được trống!"); return }
        
        isRunning = true
        appendLog("Bắt đầu Port Knocking tới \(targetIP):5555...")
        
        Task {
            do {
                let hwid = await MainActor.run { UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS" }
                let myIPs = getDeviceIPs()
                let safeLan = myIPs.lan.isEmpty ? "N/A" : myIPs.lan
                let safeTs = myIPs.ts.isEmpty ? "N/A" : myIPs.ts
                let authCmd = "AUTH_REQ|USER:\(username)|PASS:\(password)|LAN:\(safeLan)|TS:\(safeTs)|HWID:\(hwid)"
                
                let auth = try await ZhiNetworkAuth.executePortKnockingAuth(ip: targetIP, authPort: 5555, authCmd: authCmd)
                
                if auth.isSuccess {
                    appendLog("✅ Auth OK! Server cấp KCP Port: \(auth.kcpPort)")
                    
                    // 🔥 ÉP MTU XUỐNG 1200 ĐỂ CHỐNG LỖI FRAGMENT TRÊN IOS / TAILSCALE / 4G
                    let initSuccess = ZhiKcpEngine.initCore(
                        ip: targetIP, port: Int32(auth.kcpPort), 
                        symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", mtu: 1200, tuning: auth.tuning
                    )
                    
                    if initSuccess {
                        appendLog("🔥 Lõi C++ KCP & Libsodium đã nổ máy (MTU 1200)!")
                        
                        appendLog("⏳ Đang chờ Server kích hoạt Worker Socket...")
                        try await Task.sleep(nanoseconds: 2_000_000_000) // Đợi chác 2 giây
                        
                        appendLog("Gửi lệnh OP_STAT (Check rễ ổ đĩa)...")
                        let statData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: "/", offset: 0, reqLen: 0, payloadData: nil)
                        
                        appendLog("📦 KCP Phản hồi: Nhận \(statData.count) bytes thành công!")
                        appendLog("🎉 MỌI THỨ HOẠT ĐỘNG HOÀN HẢO!")
                    } else {
                        appendLog("❌ Khởi động KCP Engine thất bại.")
                        isRunning = false
                    }
                }
            } catch {
                appendLog("❌ Lỗi mạng: \(error.localizedDescription)")
                isRunning = false
            }
        }
    }
    
    func appendLog(_ msg: String) {
        DispatchQueue.main.async {
            NSLog("%@", msg)
            logText += "[\(Date().formatted(date: .omitted, time: .standard))] \(msg)\n"
        }
    }
}

import SwiftUI
import UIKit
import Darwin
import Network
import FileProvider

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

func triggerLocalNetworkPrompt() {
    let parameters = NWParameters.udp
    let endpoint = NWEndpoint.hostPort(host: "255.255.255.255", port: 9999)
    let connection = NWConnection(to: endpoint, using: parameters)
    connection.start(queue: .main)
    connection.send(content: "ZhiAuth-Dummy-Trigger".data(using: .utf8)!, completion: .contentProcessed { _ in
        connection.cancel()
    })
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
                Text("ZhiAuth v6.0").font(.largeTitle).bold()
                Text(isRunning ? "🚀 Engine đang gầm rú..." : "🛑 Đang ngủ").foregroundColor(isRunning ? .green : .red)
                VStack(spacing: 10) {
                    TextField("LAN IP Server", text: $lanIP).textFieldStyle(RoundedBorderTextFieldStyle()).keyboardType(.numbersAndPunctuation).autocapitalization(.none)
                    TextField("Tailscale IP Server", text: $tsIP).textFieldStyle(RoundedBorderTextFieldStyle()).keyboardType(.numbersAndPunctuation).autocapitalization(.none)
                    TextField("Username", text: $username).textFieldStyle(RoundedBorderTextFieldStyle()).autocapitalization(.none)
                    SecureField("Password", text: $password).textFieldStyle(RoundedBorderTextFieldStyle())
                }.padding(.horizontal)
                
                ScrollView {
                    Text(logText).font(.system(size: 10, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading).padding()
                }.background(Color.black.opacity(0.85)).foregroundColor(.green).cornerRadius(10).padding(.horizontal)
                
                Button(action: { testKcpCore() }) {
                    Text("KÍCH NỔ LÕI C++ KCP").font(.headline).foregroundColor(.white).padding().frame(maxWidth: .infinity).background(isRunning ? Color.gray : Color.blue).cornerRadius(10)
                }.disabled(isRunning).padding()
            }.onReceive(NotificationCenter.default.publisher(for: NSNotification.Name("ZhiAuthLog"))) { note in
                if let msg = note.object as? String { appendLog("C++ | " + msg) }
            }
        }
    }
    
    func testKcpCore() {
        triggerLocalNetworkPrompt()
        
        let safeLanIP = lanIP.trimmingCharacters(in: .whitespacesAndNewlines)
        let safeTsIP = tsIP.trimmingCharacters(in: .whitespacesAndNewlines)
        if safeLanIP.isEmpty && safeTsIP.isEmpty { appendLog("❌ Lỗi: Phải nhập IP Server!"); return }
        
        isRunning = true
        clearExtensionLog()
        appendLog("Bắt đầu Port Knocking (Đua tốc độ LAN & Tailscale)...")
        
        Task {
            do {
                let hwid = await MainActor.run { UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS" }
                let myIPs = getDeviceIPs()
                let safeLan = myIPs.lan.isEmpty ? "N/A" : myIPs.lan
                let safeTs = myIPs.ts.isEmpty ? "N/A" : myIPs.ts
                let authCmd = "AUTH_REQ|USER:\(username)|PASS:\(password)|LAN:\(safeLan)|TS:\(safeTs)|HWID:\(hwid)"
                
                let winner: (String, AuthResponse)? = await withTaskGroup(of: Optional<(String, AuthResponse)>.self) { group in
                    if !safeLanIP.isEmpty { group.addTask { do { return (safeLanIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeLanIP, authPort: 5555, authCmd: authCmd)) } catch { return nil } } }
                    if !safeTsIP.isEmpty { group.addTask { do { return (safeTsIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeTsIP, authPort: 5555, authCmd: authCmd)) } catch { return nil } } }
                    for await result in group { if let res = result, res.1.isSuccess { group.cancelAll(); return res } }
                    return nil
                }
                
                guard let (winningIP, auth) = winner else { appendLog("❌ Lỗi mạng: Knocking Timeout!"); isRunning = false; return }
                appendLog("✅ Auth OK qua [\(winningIP)]! Server cấp KCP Port: \(auth.kcpPort)")
                
                if ZhiKcpEngine.initCore(ip: winningIP, port: Int32(auth.kcpPort), symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", mtu: 1200, tuning: auth.tuning) {
                    appendLog("🔥 Lõi C++ KCP & Libsodium đã nổ máy!")
                    try await Task.sleep(nanoseconds: 2_000_000_000)
                    
                    let targetPath = auth.sharedPath.isEmpty ? "/tmp" : auth.sharedPath
                    let statData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: targetPath, offset: 0, reqLen: 0, payloadData: nil)
                    appendLog("🎉 Nhận OP_STAT OK (\(statData.count) Bytes)")

                    registerFileProvider()

                    isRunning = false
                } else {
                    appendLog("❌ Khởi động KCP Engine thất bại.")
                    isRunning = false
                }
            } catch {
                appendLog("❌ \(error.localizedDescription)")
                isRunning = false
            }
        }
    }
    
    func registerFileProvider() {
        guard let groupURL = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else {
            appendLog("❌ [APP-GROUP ERR] Không thể truy cập App Group! Hệ điều hành chặn hoặc sai Entitlements.")
            return
        }
        appendLog("✅ [APP-GROUP OK] Truy cập container thành công: \(groupURL.path)")
        
        let domain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier("com.ruanhuang.ZhiAuth.Cloud"), displayName: "ZhiAuth Cloud")
        appendLog("⏳ Đang gọi NSFileProviderManager.add(domain)...")
        
        NSFileProviderManager.add(domain) { error in
            if let err = error as NSError? {
                appendLog("❌ [MOUNT-ERR]: \(err.localizedDescription)")
            } else {
                appendLog("✅ Gọi lệnh cắm ổ đĩa ảo thành công!")
            }
            
            // Chờ 2 giây cho Extension nôn log ra rồi đọc
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
                self.readExtensionLog()
            }
        }
    }
    
    func clearExtensionLog() {
        guard let groupURL = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else { return }
        let logURL = groupURL.appendingPathComponent("ext_debug.log")
        try? FileManager.default.removeItem(at: logURL)
    }
    
    func readExtensionLog() {
        guard let groupURL = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else { return }
        let logURL = groupURL.appendingPathComponent("ext_debug.log")
        if let extLogs = try? String(contentsOf: logURL, encoding: .utf8) {
            appendLog("\n----- 🕵️ BÁO CÁO TỪ EXTENSION -----\n\(extLogs)")
        } else {
            appendLog("\n⚠️ [EXT-DEAD] Không có bất kỳ dòng log nào từ Extension! Tiến trình bị iOS giết trước khi hàm init() kịp chạy. (Có thể do lỗi thư viện C++ hoặc sai kiến trúc khi build).")
        }
    }

    func appendLog(_ msg: String) {
        DispatchQueue.main.async {
            logText += "[\(Date().formatted(date: .omitted, time: .standard))] \(msg)\n"
            if logText.count > 5000 { logText = String(logText.suffix(4000)) }
        }
    }
}

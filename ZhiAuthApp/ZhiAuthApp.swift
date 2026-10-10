import SwiftUI
import UIKit

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
                    
                    TextField("Tailscale IP", text: $tsIP)
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
                
                Button(action: {
                    testKcpCore()
                }) {
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
        if targetIP.isEmpty {
            appendLog("❌ Lỗi: Phải nhập ít nhất 1 IP!")
            return
        }
        if username.isEmpty || password.isEmpty {
            appendLog("❌ Lỗi: Username và Password không được để trống!")
            return
        }
        
        isRunning = true
        appendLog("Bắt đầu Port Knocking tới \(targetIP):5555...")
        
        Task {
            do {
                let hwid = await MainActor.run {
                    UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS"
                }
                
                let safeLan = lanIP.isEmpty ? "NONE" : lanIP
                let safeTs = tsIP.isEmpty ? "NONE" : tsIP
                
                // Dẹp bỏ CryptoKit, bắn thẳng mật khẩu thô y như Windows
                let authCmd = "AUTH_REQ|USER:\(username)|PASS:\(password)|LAN:\(safeLan)|TS:\(safeTs)|HWID:\(hwid)"
                
                let auth = try await ZhiNetworkAuth.executePortKnockingAuth(ip: targetIP, authPort: 5555, authCmd: authCmd)
                
                if auth.isSuccess {
                    appendLog("✅ Auth OK! Server cấp KCP Port: \(auth.kcpPort)")
                    
                    let initSuccess = ZhiKcpEngine.initCore(
                        ip: targetIP, 
                        port: Int32(auth.kcpPort), 
                        symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", 
                        mtu: 1350, 
                        tuning: auth.tuning
                    )
                    
                    if initSuccess {
                        appendLog("🔥 Lõi C++ KCP & Libsodium đã nổ máy!")
                        
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

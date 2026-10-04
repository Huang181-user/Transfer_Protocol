import SwiftUI
import UIKit

@main
struct ZhiAuthApp: App {
    @State private var logText: String = "💀 Sẵn sàng chọc thủng server...\n"
    @State private var isRunning = false
    @State private var quicTunnel: ZhiQuicTunnel?
    let serverIP = "192.168.1.83" // Ný nhớ check lại IP của con zhiserver nha

    var body: some Scene {
        WindowGroup {
            VStack(spacing: 15) {
                Text("ZhiAuth v6.0")
                    .font(.largeTitle).bold()
                
                Text(isRunning ? "🚀 Engine đang gầm rú..." : "🛑 Đang ngủ")
                    .foregroundColor(isRunning ? .green : .red)
                
                // Màn hình in log trực tiếp trên điện thoại
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
        isRunning = true
        appendLog("Bắt đầu Port Knocking tới \(serverIP):5555...")
        
        Task {
            do {
                let hwid = await MainActor.run {
                    UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS"
                }
                
                let authCmd = "AUTH_REQ|USER:huang|PASS:123456|LAN:\(serverIP)|TS:NONE|HWID:\(hwid)"
                
                let auth = try await ZhiNetworkAuth.executePortKnockingAuth(ip: serverIP, authPort: 5555, authCmd: authCmd)
                
                if auth.isSuccess {
                    appendLog("✅ Port Knocking OK! Server cấp KCP Port: \(auth.kcpPort)")
                    
                    let initSuccess = ZhiKcpEngine.initCore(
                        ip: serverIP, 
                        port: Int32(auth.kcpPort), 
                        symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", 
                        mtu: 1350, 
                        tuning: auth.tuning
                    )
                    
                    if initSuccess {
                        appendLog("🔥 Lõi C++ KCP & Libsodium đã nổ máy!")
                        
                        appendLog("Gửi lệnh OP_STAT (Check rễ ổ đĩa) qua KCP...")
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

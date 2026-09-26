import SwiftUI

@main
struct ZhiAuthApp: App {
    var body: some Scene {
        WindowGroup {
            VStack(spacing: 20) {
                Text("💀 ZhiAuth v6.0")
                    .font(.largeTitle).bold()
                Text("KCP Dual-Tunnel is running...")
                    .foregroundColor(.gray)
                Text("Hãy mở ứng dụng 'Tệp' (Files) trên iPhone để xem ổ đĩa ảo!")
                    .multilineTextAlignment(.center)
                    .padding()
            }
        }
    }
}
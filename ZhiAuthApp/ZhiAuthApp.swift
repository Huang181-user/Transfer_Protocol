import SwiftUI
import FileProvider // Bắt buộc phải import thư viện này để gọi lệnh đăng ký

@main
struct ZhiAuthApp: App {
    
    init() {
        // Đăng ký "Ổ đĩa ảo" với hệ thống iOS ngay khi vừa mở app
        let domain = NSFileProviderDomain(
            identifier: NSFileProviderDomainIdentifier(rawValue: "ZhiAuthDomain"),
            displayName: "ZhiAuth Server"
        )
        
        NSFileProviderManager.add(domain) { error in
            if let error = error {
                print("❌ Lỗi đăng ký ổ đĩa: \(error.localizedDescription)")
            } else {
                print("✅ Đã chèn ổ đĩa ảo ZhiAuth vào app Tệp thành công!")
            }
        }
    }

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
import Foundation

struct AppConfig: Codable {
    let kcpSymmetricKey: String
    let defaultAuthPort: UInt16
    let defaultQuicPort: UInt16
    let defaultKcpPort: Int
    let defaultMtu: Int32
    
    static var shared: AppConfig = {
        guard let url = Bundle.main.url(forResource: "ZhiConfig", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let config = try? JSONDecoder().decode(AppConfig.self, from: data) else {
            // Fallback an toàn nếu thiếu file
            return AppConfig(
                kcpSymmetricKey: "ZhiAuth_Secret_KCP_Key_2026_1234",
                defaultAuthPort: 5555,
                defaultQuicPort: 4433,
                defaultKcpPort: 6666,
                defaultMtu: 1200
            )
        }
        return config
    }()
}

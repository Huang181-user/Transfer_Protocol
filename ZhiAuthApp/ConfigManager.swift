import Foundation

struct AppConfig: Codable {
    let kcpSymmetricKey: String
    let defaultLanIp: String
    let defaultTsIp: String
    let defaultUsername: String
    let defaultAuthPort: UInt16
    let defaultQuicPort: UInt16
    let defaultKcpPort: Int
    let defaultMtu: Int32
    
    static var shared: AppConfig = {
        if let url = Bundle.main.url(forResource: "ZhiConfig", withExtension: "json"),
           let data = try? Data(contentsOf: url) {
            let decoder = JSONDecoder()
            decoder.keyDecodingStrategy = .convertFromSnakeCase
            if let config = try? decoder.decode(AppConfig.self, from: data) {
                return config
            }
        }
        // Fallback hoàn toàn rỗng, không lộ bất kỳ thông tin nhạy cảm nào trong mã nguồn
        return AppConfig(
            kcpSymmetricKey: "",
            defaultLanIp: "",
            defaultTsIp: "",
            defaultUsername: "",
            defaultAuthPort: 5555,
            defaultQuicPort: 4433,
            defaultKcpPort: 6666,
            defaultMtu: 1200
        )
    }()
}

import SwiftUI

enum ActiveTunnel: String {
    case none = "STANDBY"
    case kcp = "KCP"
    case quic = "QUIC"
}

struct ZhiFileItem: Identifiable, Hashable {
    let id = UUID()
    let name: String
    let isDirectory: Bool
    let size: UInt64
    let modifiedTime: UInt64
    let path: String
    
    var sizeString: String {
        if isDirectory { return "Thư mục" }
        let formatter = ByteCountFormatter()
        formatter.allowedUnits = [.useAll]
        formatter.countStyle = .file
        return formatter.string(fromByteCount: Int64(size))
    }
}

struct FileItemButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.vertical, 7)
            .padding(.horizontal, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(configuration.isPressed ? Color.blue.opacity(0.15) : Color(UIColor.secondarySystemBackground).opacity(0.4))
            .cornerRadius(8)
            .shadow(color: configuration.isPressed ? Color.black.opacity(0.18) : Color.clear, radius: 2, x: 0, y: 1)
            .scaleEffect(configuration.isPressed ? 0.98 : 1.0)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}

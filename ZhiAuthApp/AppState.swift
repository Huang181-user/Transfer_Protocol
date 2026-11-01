import SwiftUI
import Foundation

class AppState: ObservableObject {
    @Published var isLoggedIn: Bool = false
    @Published var currentPath: String = "/"
    @Published var fileList: [ZhiFileItem] = []
    @Published var isLoading: Bool = false
    @Published var logs: String = "💀 Sẵn sàng kết nối máy chủ ZhiAuth...\n"
    @Published var transferSpeed: String = "0 KB/s"
    
    @Published var clipboardItem: ZhiFileItem? = nil
    @Published var isCutOperation: Bool = false
    @Published var activeTunnel: ActiveTunnel = .none
    
    var sharedRoot: String = ""
    var baseIP: String = ""
    var kcpPort: Int = 6666
    var quicPort: UInt16 = 4433
    let quicTunnel = ZhiQuicTunnel()
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    func log(_ msg: String) {
        let ts = Self.timeFormatter.string(from: Date())
        DispatchQueue.main.async {
            self.logs += "[\(ts)] \(msg)\n"
            if self.logs.count > 12000 {
                self.logs = String(self.logs.suffix(9000))
            }
        }
    }
    
    func updateSpeed(bytes: Int, durationSeconds: Double) {
        guard durationSeconds > 0.0001 else { return }
        let bytesPerSec = Double(bytes) / durationSeconds
        let speedText: String
        if bytesPerSec >= 1024 * 1024 {
            speedText = String(format: "%.2f MB/s", bytesPerSec / (1024 * 1024))
        } else {
            speedText = String(format: "%.1f KB/s", bytesPerSec / 1024)
        }
        DispatchQueue.main.async {
            self.transferSpeed = speedText
        }
    }
    
    func displayPath(for rawPath: String) -> String {
        if rawPath == "/" { return "/ (Chọn Luồng)" }
        let prefix = activeTunnel == .kcp ? "/ZhiAuth_KCP" : "/ZhiAuth_QUIC"
        if rawPath.hasPrefix(sharedRoot) {
            let relative = String(rawPath.dropFirst(sharedRoot.count))
            if relative.isEmpty { return prefix }
            return relative.hasPrefix("/") ? (prefix + relative) : (prefix + "/" + relative)
        }
        return prefix
    }
    
    // Bộ điều phối thông minh: KCP ưu tiên, tự động nhảy sang QUIC nếu KCP gặp sự cố
    func dispatchRpc(opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) async throws -> Data {
        let startTime = CFAbsoluteTimeGetCurrent()
        let responseData: Data
        
        if activeTunnel == .quic {
            responseData = try await quicTunnel.sendRpcVfs(opcode: opcode, path: path, offset: offset, reqLen: reqLen, payloadData: payloadData)
        } else {
            do {
                responseData = try await ZhiKcpEngine.sendRpcVfs(opcode: opcode, path: path, offset: offset, reqLen: reqLen, payloadData: payloadData)
            } catch {
                log("⚠️ [FAILOVER] KCP gặp sự cố (\(error.localizedDescription)). Tự động chuyển sang luồng dự phòng QUIC!")
                responseData = try await quicTunnel.sendRpcVfs(opcode: opcode, path: path, offset: offset, reqLen: reqLen, payloadData: payloadData)
            }
        }
        
        let elapsed = CFAbsoluteTimeGetCurrent() - startTime
        let transferredBytes = max(responseData.count, payloadData?.count ?? 0)
        if transferredBytes > 64 {
            updateSpeed(bytes: transferredBytes, durationSeconds: elapsed)
        }
        return responseData
    }
}

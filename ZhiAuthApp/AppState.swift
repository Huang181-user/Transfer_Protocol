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
    
    // Danh sách các file được Share từ ứng dụng khác đang chờ người dùng chọn thư mục để lưu
    @Published var pendingSharedURLs: [URL] = []
    
    private var watchdogTimer: Timer?
    func startWatchdog() {
        watchdogTimer?.invalidate()
        watchdogTimer = Timer.scheduledTimer(withTimeInterval: 15.0, repeats: true) { [weak self] _ in
            guard let self = self, self.isLoggedIn else { return }
            Task {
                do { _ = try await self.dispatchRpc(opcode: .OP_PING, path: "", offset: 0, reqLen: 0, payloadData: nil) } catch { self.log("⚠️ [WATCHDOG] Lỗi Ping") }
                let currentIPs = getDeviceIPs()
                let curLan = currentIPs.lan.isEmpty ? "N/A" : currentIPs.lan
                let curTs = currentIPs.ts.isEmpty ? "N/A" : currentIPs.ts
                if curLan != self.lastLanIP || curTs != self.lastTsIP {
                    self.lastLanIP = curLan; self.lastTsIP = curTs
                    self.log("⚡ [ROAMING] Đổi mạng! Khôi phục phiên...")
                    await self.triggerEmergencyReauth(lanIP: curLan, tsIP: curTs)
                }
            }
        }
    }
    var lastLanIP: String = ""
    var lastTsIP: String = ""
    var sessionToken: String = ""
    var hardwareMac: String = UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN"
    private func triggerEmergencyReauth(lanIP: String, tsIP: String) async {
        let authCmd = "ROAM_REQ|TOKEN:\(sessionToken)|LAN:\(lanIP)|TS:\(tsIP)|HWID:\(hardwareMac)"
        do {
            var newBaseIP = ""
            var resp: AuthResponse? = nil
            if lanIP != "N/A" {
                let ipToKnock = AppConfig.shared.defaultLanIp
                if !ipToKnock.isEmpty { resp = try? await ZhiNetworkAuth.executePortKnockingAuth(ip: ipToKnock, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd); if resp?.isSuccess == true { newBaseIP = ipToKnock } }
            }
            if resp == nil && tsIP != "N/A" {
                let ipToKnock = AppConfig.shared.defaultTsIp
                if !ipToKnock.isEmpty { resp = try? await ZhiNetworkAuth.executePortKnockingAuth(ip: ipToKnock, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd); if resp?.isSuccess == true { newBaseIP = ipToKnock } }
            }
            if let auth = resp, auth.isSuccess {
                self.baseIP = newBaseIP; self.kcpPort = auth.kcpPort; self.quicPort = UInt16(auth.quicPort)
                _ = ZhiKcpEngine.initCore(ip: self.baseIP, port: Int32(self.kcpPort), symKey: auth.symKey, mtu: AppConfig.shared.defaultMtu, tuning: auth.tuning)
                self.quicTunnel.configure(ip: self.baseIP, port: self.quicPort)
                self.log("✅ [RE-AUTH] Khôi phục phiên qua \(self.baseIP)")
            }
        } catch { self.log("❌ [RE-AUTH] Rớt mạng...") }
    }
    var sharedRoot: String = ""
    private var watchdogTimer: Timer?
    func startWatchdog() {
        watchdogTimer?.invalidate()
        watchdogTimer = Timer.scheduledTimer(withTimeInterval: 15.0, repeats: true) { [weak self] _ in
            guard let self = self, self.isLoggedIn else { return }
            Task {
                do { _ = try await self.dispatchRpc(opcode: .OP_PING, path: "", offset: 0, reqLen: 0, payloadData: nil) } catch { self.log("⚠️ [WATCHDOG] Lỗi Ping") }
                let currentIPs = getDeviceIPs()
                let curLan = currentIPs.lan.isEmpty ? "N/A" : currentIPs.lan
                let curTs = currentIPs.ts.isEmpty ? "N/A" : currentIPs.ts
                if curLan != self.lastLanIP || curTs != self.lastTsIP {
                    self.lastLanIP = curLan; self.lastTsIP = curTs
                    self.log("⚡ [ROAMING] Đổi mạng! Khôi phục phiên...")
                    await self.triggerEmergencyReauth(lanIP: curLan, tsIP: curTs)
                }
            }
        }
    }
    
    var lastLanIP: String = ""
    var lastTsIP: String = ""
    var sessionToken: String = ""
    var hardwareMac: String = UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN"
    
    private func triggerEmergencyReauth(lanIP: String, tsIP: String) async {
        let authCmd = "ROAM_REQ|TOKEN:\(sessionToken)|LAN:\(lanIP)|TS:\(tsIP)|HWID:\(hardwareMac)"
        do {
            var newBaseIP = ""
            var resp: AuthResponse? = nil
            if lanIP != "N/A" {
                let ipToKnock = AppConfig.shared.defaultLanIp
                if !ipToKnock.isEmpty { resp = try? await ZhiNetworkAuth.executePortKnockingAuth(ip: ipToKnock, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd); if resp?.isSuccess == true { newBaseIP = ipToKnock } }
            }
            if resp == nil && tsIP != "N/A" {
                let ipToKnock = AppConfig.shared.defaultTsIp
                if !ipToKnock.isEmpty { resp = try? await ZhiNetworkAuth.executePortKnockingAuth(ip: ipToKnock, authPort: AppConfig.shared.defaultAuthPort, authCmd: authCmd); if resp?.isSuccess == true { newBaseIP = ipToKnock } }
            }
            if let auth = resp, auth.isSuccess {
                self.baseIP = newBaseIP; self.kcpPort = auth.kcpPort; self.quicPort = UInt16(auth.quicPort)
                _ = ZhiKcpEngine.initCore(ip: self.baseIP, port: Int32(self.kcpPort), symKey: auth.symKey, mtu: AppConfig.shared.defaultMtu, tuning: auth.tuning)
                self.quicTunnel.configure(ip: self.baseIP, port: self.quicPort)
                self.log("✅ [RE-AUTH] Khôi phục phiên qua \(self.baseIP)")
            }
        } catch { self.log("❌ [RE-AUTH] Rớt mạng...") }
    }

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
    
    // Tiêu đề thanh điều hướng không bao giờ làm lộ tên thư mục vật lý trên Server
    var currentFolderTitle: String {
        if currentPath == "/" { return "ZhiAuth Explorer" }
        let prefix = activeTunnel == .kcp ? "ZhiAuth_KCP" : "ZhiAuth_QUIC"
        if currentPath == sharedRoot { return prefix }
        return URL(fileURLWithPath: currentPath).lastPathComponent
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
    
    // Nhận file được Share từ ứng dụng khác (Photos, Files, Zalo...) và lưu vào bộ nhớ đệm chờ chọn đường dẫn
    func enqueueSharedFile(url: URL) {
        let hasScope = url.startAccessingSecurityScopedResource()
        defer { if hasScope { url.stopAccessingSecurityScopedResource() } }
        
        let tempDir = FileManager.default.temporaryDirectory.appendingPathComponent("ZhiSharedInbox", isDirectory: true)
        try? FileManager.default.createDirectory(at: tempDir, withIntermediateDirectories: true)
        let destURL = tempDir.appendingPathComponent(url.lastPathComponent)
        
        do {
            if FileManager.default.fileExists(atPath: destURL.path) {
                try FileManager.default.removeItem(at: destURL)
            }
            try FileManager.default.copyItem(at: url, to: destURL)
            DispatchQueue.main.async {
                if !self.pendingSharedURLs.contains(destURL) {
                    self.pendingSharedURLs.append(destURL)
                }
                self.log("📎 Đã nhận tệp '\(destURL.lastPathComponent)' từ Share Sheet. Hãy mở thư mục đích rồi bấm [Lưu vào đây].")
            }
        } catch {
            log("❌ Không thể nhận tệp chia sẻ: \(error.localizedDescription)")
        }
    }
    
    func clearPendingSharedFiles() {
        for url in pendingSharedURLs {
            try? FileManager.default.removeItem(at: url)
        }
        DispatchQueue.main.async {
            self.pendingSharedURLs.removeAll()
        }
    }
    
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

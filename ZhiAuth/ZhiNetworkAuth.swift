import Foundation
import Network

public struct KcpTuningParams {
    public var noDelay: Int32 = 1
    public var interval: Int32 = 10
    public var resend: Int32 = 2
    public var nc: Int32 = 0
    public var sndWnd: Int32 = 512
    public var rcvWnd: Int32 = 512
    public var isDynamic: Bool = false
    public init() {}
}

public struct AuthResponse {
    public var isSuccess: Bool = false
    public var sharedPath: String = ""
    public var quicPort: Int = 4433
    public var kcpPort: Int = 6666
    public var symKey: String = ""
    public var sessionToken: String = ""
    public var tuning: KcpTuningParams = KcpTuningParams()
    public init() {}
}

final class AuthState: @unchecked Sendable {
    private let lock = NSLock()
    private var _isResponded = false
    
    func claimResponse() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if _isResponded { return false }
        _isResponded = true
        return true
    }
}

public class ZhiNetworkAuth {
    private static func logRealtime(_ msg: String) {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSS"
        let ts = formatter.string(from: Date())
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthLog"), object: "AUTH | [\(ts)] \(msg)")
        }
    }

    public static func executePortKnockingAuth(ip: String, authPort: UInt16, authCmd: String) async throws -> AuthResponse {
        return try await withCheckedThrowingContinuation { continuation in
            let host = NWEndpoint.Host(ip)
            guard let port = NWEndpoint.Port(rawValue: authPort) else {
                continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -400, userInfo: [NSLocalizedDescriptionKey: "Cổng mạng không hợp lệ"]))
                return
            }
            
            let options = NWProtocolQUIC.Options(alpn: ["zhiauth-rpc"])
            let secOptions = options.securityProtocolOptions
            sec_protocol_options_set_verify_block(secOptions, { _, _, sec_protocol_verify_complete in
                sec_protocol_verify_complete(true)
            }, .main)
            
            let parameters = NWParameters(quic: options)
            let connection = NWConnection(host: host, port: port, using: parameters)
            let serialQueue = DispatchQueue(label: "com.zhiauth.quic_auth.\(ip)")
            
            let state = AuthState()
            
            serialQueue.asyncAfter(deadline: .now() + 5.0) {
                if state.claimResponse() {
                    connection.cancel()
                    logRealtime("⏱️ [TIMEOUT] Quá hạn 5s khi gõ cửa \(ip):\(authPort)")
                    let err = NSError(domain: "ZhiAuth.Network", code: -408, userInfo: [NSLocalizedDescriptionKey: "Timeout 5s: Không kết nối được tới \(ip)"])
                    continuation.resume(throwing: err)
                }
            }
            
            connection.stateUpdateHandler = { connState in
                switch connState {
                case .ready:
                    guard let cmdData = authCmd.data(using: .utf8) else { return }
                    logRealtime("📤 [TX-AUTH] Đã nối QUIC tới \(ip):\(authPort), đang bắn gói AUTH (\(cmdData.count)B) kèm cờ FIN...")
                    
                    // 🔥 FIX: Bổ sung contentContext: .finalMessage và isComplete: true để Server C++ bắt được QUIC_RECEIVE_FLAG_FIN
                    connection.send(content: cmdData, contentContext: .finalMessage, isComplete: true, completion: .contentProcessed { sendError in
                        if let sendError = sendError {
                            connection.cancel()
                            logRealtime("❌ [TX-ERR] Lỗi gửi AUTH tới \(ip): \(sendError.localizedDescription)")
                            if state.claimResponse() { continuation.resume(throwing: sendError) }
                            return
                        }
                        
                        logRealtime("📥 [RX-WAIT] Đã bắn gói tin thành công, chờ MsQUIC Server phản hồi...")
                        connection.receive(minimumIncompleteLength: 1, maximumLength: 2048) { data, _, _, recvError in
                            connection.cancel()
                            guard state.claimResponse() else { return }
                            
                            if let recvError = recvError {
                                logRealtime("❌ [RX-ERR] Lỗi nhận phản hồi từ \(ip): \(recvError.localizedDescription)")
                                continuation.resume(throwing: recvError)
                                return
                            }
                            
                            if let data = data, let respStr = String(data: data, encoding: .utf8) {
                                logRealtime("📥 [RX-AUTH] Phản hồi từ \(ip): \(respStr)")
                                if respStr.hasPrefix("AUTH_SUCCESS") {
                                    var resp = AuthResponse()
                                    resp.isSuccess = true
                                    resp.symKey = AppConfig.shared.kcpSymmetricKey
                                    let parts = respStr.split(separator: "|")
                                    if parts.count >= 4 {
                                        resp.sharedPath = String(parts[1])
                                        resp.quicPort = Int(parts[2]) ?? 4433
                                        resp.kcpPort = Int(parts[3]) ?? 6666
                                    }
                                    if parts.count >= 10 {
                                        resp.tuning.noDelay = Int32(parts[4]) ?? 1
                                        resp.tuning.interval = Int32(parts[5]) ?? 10
                                        resp.tuning.resend = Int32(parts[6]) ?? 2
                                        resp.tuning.nc = Int32(parts[7]) ?? 0
                                        resp.tuning.sndWnd = Int32(parts[8]) ?? 512
                                        resp.tuning.rcvWnd = Int32(parts[9]) ?? 512
                                        resp.tuning.isDynamic = true
                                    }
                                    // Parse Session Token
                                    let authParts = authCmd.split(separator: "|")
                                    for part in authParts {
                                        if part.hasPrefix("TOKEN:") {
                                            resp.sessionToken = String(part.dropFirst("TOKEN:".count))
                                        }
                                    }
                                    logRealtime("✅ [AUTH-SUCCESS] Parse dữ liệu thành công, trả về AppState.")
                                    continuation.resume(returning: resp)
                                } else {
                                    continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -401, userInfo: [NSLocalizedDescriptionKey: "Server từ chối xác thực: \(respStr)"]))
                                }
                            } else {
                                logRealtime("❌ [RX-EMPTY] Gói phản hồi rỗng từ \(ip)")
                                continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -401, userInfo: [NSLocalizedDescriptionKey: "Server đóng kết nối không phản hồi"]))
                            }
                        }
                    })
                case .failed(let err):
                    logRealtime("❌ [CONN-FAIL] Kết nối QUIC tới \(ip) thất bại: \(err.localizedDescription)")
                    if state.claimResponse() { continuation.resume(throwing: err) }
                case .cancelled:
                    if state.claimResponse() {
                        continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -402, userInfo: [NSLocalizedDescriptionKey: "Đã huỷ kết nối"]))
                    }
                case .waiting(let err):
                    logRealtime("⚠️ [CONN-WAIT] Đường truyền tới \(ip) đang nghẽn: \(err.localizedDescription)")
                    connection.cancel()
                    if state.claimResponse() {
                        continuation.resume(throwing: err)
                    }
                default: break
                }
            }
            connection.start(queue: serialQueue)
        }
    }
}
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
            let serialQueue = DispatchQueue(label: "com.zhiauth.quic_auth")
            
            let state = AuthState()
            
            // HẸN GIỜ NỔ: Sau đúng 3 giây nếu chưa kết nối xong thì cắt cầu dao báo lỗi thẳng lên màn hình!
            serialQueue.asyncAfter(deadline: .now() + 3.0) {
                if state.claimResponse() {
                    connection.cancel()
                    let err = NSError(domain: "ZhiAuth.Network", code: -408, userInfo: [NSLocalizedDescriptionKey: "Timeout 3s: Không kết nối được tới \(ip)"])
                    continuation.resume(throwing: err)
                }
            }
            
            connection.stateUpdateHandler = { connState in
                switch connState {
                case .ready:
                    guard let cmdData = authCmd.data(using: .utf8) else { return }
                    connection.send(content: cmdData, completion: .contentProcessed { sendError in
                        if let sendError = sendError {
                            connection.cancel()
                            if state.claimResponse() { continuation.resume(throwing: sendError) }
                            return
                        }
                        
                        connection.receive(minimumIncompleteLength: 1, maximumLength: 1024) { data, _, _, _ in
                            connection.cancel()
                            guard state.claimResponse() else { return }
                            
                            if let data = data, let respStr = String(data: data, encoding: .utf8), respStr.hasPrefix("AUTH_SUCCESS") {
                                var resp = AuthResponse()
                                resp.isSuccess = true
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
                                continuation.resume(returning: resp)
                            } else {
                                continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -401, userInfo: [NSLocalizedDescriptionKey: "Server từ chối xác thực"]))
                            }
                        }
                    })
                case .failed(let err):
                    if state.claimResponse() { continuation.resume(throwing: err) }
                case .cancelled:
                    if state.claimResponse() {
                        continuation.resume(throwing: NSError(domain: "ZhiAuth.Network", code: -402, userInfo: [NSLocalizedDescriptionKey: "Đã huỷ kết nối"]))
                    }
                case .waiting(let err):
                    // Bắt chặt case ngắt mạng hoặc sai IP
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

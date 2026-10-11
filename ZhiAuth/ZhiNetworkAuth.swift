import Foundation
import Network

struct AuthResponse {
    var isSuccess: Bool = false
    var sharedPath: String = ""
    var quicPort: Int = 4433
    var kcpPort: Int = 6666
    var tuning: KcpTuningParams = KcpTuningParams()
}

// Lớp an toàn đa luồng chứa trạng thái
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

class ZhiNetworkAuth {
    static func executePortKnockingAuth(ip: String, authPort: UInt16, authCmd: String) async throws -> AuthResponse {
        return try await withCheckedThrowingContinuation { continuation in
            let host = NWEndpoint.Host(ip)
            guard let port = NWEndpoint.Port(rawValue: authPort) else {
                continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -400, userInfo: [NSLocalizedDescriptionKey: "Invalid Port"]))
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
            
            connection.stateUpdateHandler = { connState in
                switch connState {
                case .ready:
                    guard let cmdData = authCmd.data(using: .utf8) else { return }
                    connection.send(content: cmdData, completion: .contentProcessed { sendError in
                        if let sendError = sendError {
                            connection.cancel()
                            if state.claimResponse() {
                                continuation.resume(throwing: sendError)
                            }
                            return
                        }
                        
                        connection.receive(minimumIncompleteLength: 1, maximumLength: 1024) { data, _, _, _ in
                            connection.cancel()
                            guard state.claimResponse() else { return }
                            
                            if let data = data, let respStr = String(data: data, encoding: .utf8), respStr.hasPrefix("AUTH_SUCCESS") {
                                var resp = AuthResponse(isSuccess: true)
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
                                continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -401, userInfo: [NSLocalizedDescriptionKey: "Authentication Failed"]))
                            }
                        }
                    })
                case .failed(let err):
                    if state.claimResponse() { continuation.resume(throwing: err) }
                case .cancelled:
                    if state.claimResponse() {
                        continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -402, userInfo: [NSLocalizedDescriptionKey: "Connection Cancelled"]))
                    }
                default: break
                }
            }
            connection.start(queue: serialQueue)
        }
    }
}

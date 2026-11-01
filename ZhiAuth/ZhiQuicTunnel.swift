import Foundation
import Network

final class QuicRpcState: @unchecked Sendable {
    private let lock = NSLock()
    private var _isCompleted = false
    var buffer = Data()
    
    func tryComplete() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if _isCompleted { return false }
        _isCompleted = true
        return true
    }
    
    func appendData(_ data: Data) {
        lock.lock()
        buffer.append(data)
        lock.unlock()
    }
    
    func getData() -> Data {
        lock.lock()
        defer { lock.unlock() }
        return buffer
    }
}

public class ZhiQuicTunnel: @unchecked Sendable {
    private var serverIP: String = ""
    private var serverPort: UInt16 = 4433
    private var reqCounter: UInt32 = 10000
    public private(set) var isConfigured: Bool = false
    
    public init() {}
    
    private func logRealtime(_ msg: String) {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSSSSS"
        let ts = formatter.string(from: Date())
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthLog"), object: "QUIC | [\(ts)] \(msg)")
        }
    }
    
    public func configure(ip: String, port: UInt16) {
        self.serverIP = ip
        self.serverPort = port
        self.isConfigured = true
        logRealtime("⚡️ [QUIC-READY] Đã cấu hình đường hầm QUIC tại \(ip):\(port)")
    }
    
    public func disconnect() {
        self.isConfigured = false
        logRealtime("🛑 [QUIC-STOP] Đã ngắt đường hầm QUIC")
    }
    
    private func buildVfsPacket(reqId: UInt32, opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) -> Data {
        let pathData = Data(path.utf8)
        let payloadCount = UInt32(payloadData?.count ?? 0)
        let finalDataLen = (reqLen > 0) ? reqLen : payloadCount
        
        var packet = Data()
        packet.reserveCapacity(7 + 27 + pathData.count + Int(payloadCount))
        
        // Tiền tố FS_CMD| khớp với MsQuicServer::DataStreamCallback trên Server
        packet.append(contentsOf: "FS_CMD|".utf8)
        
        var magic: UInt32 = UInt32(0x5A484941).littleEndian
        var op: UInt8 = opcode.rawValue
        var sessionId: UInt64 = UInt64(reqId).littleEndian
        var off: UInt64 = offset.littleEndian
        var dLen: UInt32 = finalDataLen.littleEndian
        var pLen: UInt16 = UInt16(pathData.count).littleEndian
        
        withUnsafeBytes(of: &magic) { packet.append(contentsOf: $0) }
        withUnsafeBytes(of: &op) { packet.append(contentsOf: $0) }
        withUnsafeBytes(of: &sessionId) { packet.append(contentsOf: $0) }
        withUnsafeBytes(of: &off) { packet.append(contentsOf: $0) }
        withUnsafeBytes(of: &dLen) { packet.append(contentsOf: $0) }
        withUnsafeBytes(of: &pLen) { packet.append(contentsOf: $0) }
        
        packet.append(pathData)
        if let payloadData = payloadData, !payloadData.isEmpty {
            packet.append(payloadData)
        }
        return packet
    }
    
    public func sendRpcVfs(opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) async throws -> Data {
        guard isConfigured, !serverIP.isEmpty, let port = NWEndpoint.Port(rawValue: serverPort) else {
            throw NSError(domain: "ZhiAuth.QUIC", code: -601, userInfo: [NSLocalizedDescriptionKey: "Luồng QUIC chưa được cấu hình"])
        }
        
        reqCounter &+= 1
        let reqId = reqCounter
        let packet = buildVfsPacket(reqId: reqId, opcode: opcode, path: path, offset: offset, reqLen: reqLen, payloadData: payloadData)
        
        logRealtime("📤 [TX-QUIC] Bắn gói RPC | Opcode: 0x\(String(format: "%02X", opcode.rawValue)) | ReqID: \(reqId) | Size: \(packet.count)B")
        
        return try await withCheckedThrowingContinuation { continuation in
            let host = NWEndpoint.Host(serverIP)
            let options = NWProtocolQUIC.Options(alpn: ["zhiauth-rpc"])
            let secOptions = options.securityProtocolOptions
            sec_protocol_options_set_verify_block(secOptions, { _, _, complete in
                complete(true)
            }, .main)
            
            let params = NWParameters(quic: options)
            let conn = NWConnection(host: host, port: port, using: params)
            let queue = DispatchQueue(label: "com.zhiauth.quic.rpc.\(reqId)")
            let rpcState = QuicRpcState()
            
            queue.asyncAfter(deadline: .now() + 8.0) {
                if rpcState.tryComplete() {
                    conn.cancel()
                    self.logRealtime("❌ [TIMEOUT-QUIC] Quá hạn 8s cho ReqID: \(reqId)")
                    continuation.resume(throwing: NSError(domain: "ZhiAuth.QUIC", code: -408, userInfo: [NSLocalizedDescriptionKey: "QUIC Timeout 8s (ReqID: \(reqId))"]))
                }
            }
            
            @Sendable func receiveNextChunk() {
                conn.receive(minimumIncompleteLength: 1, maximumLength: 131072) { content, _, isComplete, error in
                    if let content = content, !content.isEmpty {
                        rpcState.appendData(content)
                    }
                    if let error = error {
                        conn.cancel()
                        if rpcState.tryComplete() {
                            continuation.resume(throwing: error)
                        }
                        return
                    }
                    if isComplete {
                        conn.cancel()
                        if rpcState.tryComplete() {
                            let finalData = rpcState.getData()
                            self.logRealtime("📥 [RX-QUIC] Nhận đủ gói QUIC | ReqID: \(reqId) | Tổng: \(finalData.count)B")
                            if finalData.count >= 5 && finalData[4] == VfsOpcode.OP_ERROR.rawValue {
                                continuation.resume(throwing: NSError(domain: "ZhiAuth.QUIC", code: -502, userInfo: [NSLocalizedDescriptionKey: "Server QUIC trả về OP_ERROR (0xFF)"]))
                            } else {
                                continuation.resume(returning: finalData)
                            }
                        }
                    } else {
                        receiveNextChunk()
                    }
                }
            }
            
            conn.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    // Gửi kèm cờ isComplete: true để kích hoạt QUIC_RECEIVE_FLAG_FIN trên MsQuicServer
                    conn.send(content: packet, contentContext: .finalMessage, isComplete: true, completion: .contentProcessed { sendErr in
                        if let sendErr = sendErr {
                            conn.cancel()
                            if rpcState.tryComplete() {
                                continuation.resume(throwing: sendErr)
                            }
                            return
                        }
                        receiveNextChunk()
                    })
                case .failed(let err):
                    conn.cancel()
                    if rpcState.tryComplete() {
                        continuation.resume(throwing: err)
                    }
                case .waiting(let err):
                    conn.cancel()
                    if rpcState.tryComplete() {
                        continuation.resume(throwing: err)
                    }
                default:
                    break
                }
            }
            
            conn.start(queue: queue)
        }
    }
}

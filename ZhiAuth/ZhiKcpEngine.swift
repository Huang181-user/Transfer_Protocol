import Foundation

struct KcpTuningParams {
    var noDelay: Int32 = 1
    var interval: Int32 = 10
    var resend: Int32 = 2
    var nc: Int32 = 0
    var sndWnd: Int32 = 512
    var rcvWnd: Int32 = 512
    var isDynamic: Bool = false
}

enum VfsOpcode: UInt8 {
    case OP_PING     = 0x00
    case OP_STAT     = 0x01
    case OP_LIST     = 0x02
    case OP_READ     = 0x03
    case OP_WRITE    = 0x04
    case OP_MKDIR    = 0x05
    case OP_DELETE   = 0x06
    case OP_RENAME   = 0x07
    case OP_TRUNCATE = 0x08
    case OP_ERROR    = 0xFF
}

private var pendingRequests: [UInt32: (Data) -> Void] = [:]
private let requestLock = NSLock()
private var globalReqId: UInt32 = 0
private let appClientID: UInt32 = UInt32((Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 1) * 1_000_000_000).rounded())

@_cdecl("zhiauth_cgo_on_response")
public func zhiauth_cgo_on_response(reqId: UInt64, data: UnsafePointer<UInt8>?, length: Int) {
    guard let dataPtr = data, length > 0 else {
        ZhiLogger.error("C++ Core returned EMPTY payload for Request ID: \(reqId)!")
        return
    }
    
    let goReqId = UInt32(truncatingIfNeeded: reqId)
    let responseData = Data(bytes: dataPtr, count: length)
    
    requestLock.lock()
    let completion = pendingRequests[goReqId]
    pendingRequests.removeValue(forKey: goReqId)
    requestLock.unlock()
    
    if let completion = completion {
        completion(responseData)
    } else {
        ZhiLogger.warning("Phantom callback! Request ID \(goReqId) not found in pending queue.")
    }
}

class ZhiKcpEngine {
    static func initCore(ip: String, port: Int32, symKey: String, mtu: Int32, tuning: KcpTuningParams) -> Bool {
        ZhiLogger.info("Igniting C++ KCP Engine... Target: \(ip):\(port)")
        
        let result = ip.withCString { ipPtr in
            symKey.withCString { keyPtr in
                zhiauth_client_init(
                    ipPtr, port, keyPtr, mtu,
                    tuning.noDelay, tuning.interval, tuning.resend, tuning.nc,
                    tuning.sndWnd, tuning.rcvWnd
                )
            }
        }
        
        if result == 0 {
            ZhiLogger.info("C++ KCP Engine Operational!")
            return true
        } else {
            ZhiLogger.error("C++ KCP Engine Initialization FAILED!")
            return false
        }
    }
    
    static func shutdownCore() {
        ZhiLogger.warning("Halting C++ KCP Engine...")
        zhiauth_client_shutdown()
    }
    
    static func sendRpcVfs(opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) async throws -> Data {
        return try await withCheckedThrowingContinuation { continuation in
            var buf = Data()
            
            requestLock.lock()
            globalReqId &+= 1
            let currentReqId = globalReqId
            requestLock.unlock()
            
            let combinedSessionId: UInt64 = (UInt64(appClientID) << 32) | UInt64(currentReqId)
            let dataLen: UInt32 = (opcode == .OP_READ) ? reqLen : UInt32(payloadData?.count ?? 0)
            let pathData = path.data(using: .utf8) ?? Data()
            let pathLen: UInt16 = UInt16(pathData.count)
            
            withUnsafeBytes(of: UInt32(0x5A484941).littleEndian) { buf.append(contentsOf: $0) }
            buf.append(opcode.rawValue)
            withUnsafeBytes(of: combinedSessionId.littleEndian) { buf.append(contentsOf: $0) }
            withUnsafeBytes(of: offset.littleEndian) { buf.append(contentsOf: $0) }
            withUnsafeBytes(of: dataLen.littleEndian) { buf.append(contentsOf: $0) }
            withUnsafeBytes(of: pathLen.littleEndian) { buf.append(contentsOf: $0) }
            
            buf.append(pathData)
            if let pData = payloadData {
                buf.append(pData)
            }
            
            ZhiLogger.debug("Pumping Request [ID: \(currentReqId)] [OP: \(opcode.rawValue)] down to C++ Core... (Payload Size: \(buf.count) bytes)")
            
            requestLock.lock()
            pendingRequests[currentReqId] = { responseBytes in
                if responseBytes.count < 27 {
                    continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -1, userInfo: [NSLocalizedDescriptionKey: "VFS Packet Corrupted"]))
                    return
                }
                if responseBytes[4] == VfsOpcode.OP_ERROR.rawValue {
                    continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -2, userInfo: [NSLocalizedDescriptionKey: "Server VFS Error"]))
                    return
                }
                let actualData = responseBytes.subdata(in: 27..<responseBytes.count)
                continuation.resume(returning: actualData)
            }
            requestLock.unlock()
            
            Task {
                try? await Task.sleep(nanoseconds: 15_000_000_000)
                var didTimeout = false
                requestLock.lock()
                if pendingRequests.keys.contains(currentReqId) {
                    pendingRequests.removeValue(forKey: currentReqId)
                    didTimeout = true
                }
                requestLock.unlock()
                
                if didTimeout {
                    ZhiLogger.error("CRITICAL! Signal lost. 15-second Timeout on Request ID: \(currentReqId) [OP: \(opcode.rawValue)] [Path: \(path)]")
                    continuation.resume(throwing: NSError(domain: "ZhiAuth", code: -3, userInfo: [NSLocalizedDescriptionKey: "VFS RPC Timeout"]))
                }
            }
            
            buf.withUnsafeBytes { rawBuffer in
                if let ptr = rawBuffer.bindMemory(to: UInt8.self).baseAddress {
                    zhiauth_send_vfs_command_async(ptr, buf.count)
                }
            }
        }
    }
}
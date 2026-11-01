import Foundation

@_cdecl("zhiauth_swift_log")
public func zhiauth_swift_log(msg: UnsafePointer<CChar>) {
    let str = String(cString: msg)
    DispatchQueue.main.async {
        NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthLog"), object: str)
    }
}

actor KcpRequestManager {
    private var pendingRequests: [UInt32: CheckedContinuation<Data, Error>] = [:]
    
    func addRequest(id: UInt32, continuation: CheckedContinuation<Data, Error>) {
        pendingRequests[id] = continuation
    }
    
    func completeRequest(id: UInt32, data: Data) {
        if let continuation = pendingRequests.removeValue(forKey: id) {
            continuation.resume(returning: data)
        }
    }
    
    func removeRequest(id: UInt32) {
        pendingRequests.removeValue(forKey: id)
    }
    
    func failAll(error: Error) {
        for (_, continuation) in pendingRequests {
            continuation.resume(throwing: error)
        }
        pendingRequests.removeAll()
    }
}

public enum VfsOpcode: UInt8 {
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

public class ZhiKcpEngine {
    private static var vfsClient: UnsafeMutableRawPointer? = nil
    fileprivate static let requestManager = KcpRequestManager()
    private static var currentSessionId: UInt32 = 0
    
    public static func initCore(ip: String, port: Int32, symKey: String, mtu: Int32, tuning: KcpTuningParams) -> Bool {
        stopCore()
        vfsClient = zhiauth_create_vfs_client(
            ip, port, symKey, mtu,
            tuning.noDelay, tuning.interval, tuning.resend, tuning.nc,
            tuning.sndWnd, tuning.rcvWnd
        )
        guard let client = vfsClient else { return false }
        return zhiauth_start_vfs_client(client)
    }
    
    public static func stopCore() {
        guard let client = vfsClient else { return }
        zhiauth_stop_vfs_client(client)
        zhiauth_destroy_vfs_client(client)
        vfsClient = nil
        Task {
            await requestManager.failAll(error: NSError(domain: "ZhiAuth.KCP", code: -500, userInfo: [NSLocalizedDescriptionKey: "KCP Engine Stopped"]))
        }
    }
    
    public static func sendRpcVfs(opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) async throws -> Data {
        guard let client = vfsClient else {
            throw NSError(domain: "ZhiAuth.KCP", code: -501, userInfo: [NSLocalizedDescriptionKey: "Lõi KCP chưa khởi động"])
        }
        
        currentSessionId &+= 1
        let reqId = currentSessionId
        
        return try await withThrowingTaskGroup(of: Data.self) { group in
            group.addTask {
                return try await withCheckedThrowingContinuation { continuation in
                    Task {
                        await requestManager.addRequest(id: reqId, continuation: continuation)
                        path.withCString { cPath in
                            if let data = payloadData, !data.isEmpty {
                                data.withUnsafeBytes { rawPtr in
                                    let basePtr = rawPtr.bindMemory(to: UInt8.self).baseAddress
                                    zhiauth_vfs_send_rpc(client, reqId, opcode.rawValue, cPath, offset, reqLen, basePtr, UInt32(data.count))
                                }
                            } else {
                                zhiauth_vfs_send_rpc(client, reqId, opcode.rawValue, cPath, offset, reqLen, nil, 0)
                            }
                        }
                    }
                }
            }
            
            group.addTask {
                try await Task.sleep(nanoseconds: 6_000_000_000)
                await requestManager.removeRequest(id: reqId)
                let errMsg = "[TIMEOUT] KCP quá hạn 6s (ReqID: \(reqId))"
                NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthLog"), object: "❌ " + errMsg)
                throw NSError(domain: "ZhiAuth.KCP", code: -408, userInfo: [NSLocalizedDescriptionKey: errMsg])
            }
            
            let result = try await group.next()!
            group.cancelAll()
            
            if result.count >= 5 && result[4] == VfsOpcode.OP_ERROR.rawValue {
                throw NSError(domain: "ZhiAuth.KCP", code: -502, userInfo: [NSLocalizedDescriptionKey: "Server trả về OP_ERROR (0xFF) cho lệnh \(opcode)"])
            }
            return result
        }
    }
}

@_cdecl("zhiauth_swift_on_response")
public func zhiauth_swift_on_response(reqId: UInt32, payload: UnsafePointer<UInt8>?, len: UInt32) {
    let data = payload != nil ? Data(bytes: payload!, count: Int(len)) : Data()
    Task { await ZhiKcpEngine.requestManager.completeRequest(id: reqId, data: data) }
}

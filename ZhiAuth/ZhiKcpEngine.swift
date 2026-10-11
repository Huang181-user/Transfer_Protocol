import Foundation

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
    
    func failAll(error: Error) {
        for (_, continuation) in pendingRequests {
            continuation.resume(throwing: error)
        }
        pendingRequests.removeAll()
    }
}

public enum VfsOpcode: UInt8 {
    case OP_STAT = 1
    case OP_READ = 2
    case OP_WRITE = 3
    case OP_MKDIR = 4
    case OP_DELETE = 5
    case OP_LIST = 6
}

public class ZhiKcpEngine {
    private static var vfsClient: UnsafeMutableRawPointer? = nil
    fileprivate static let requestManager = KcpRequestManager()
    private static var currentSessionId: UInt32 = 0
    
    public static func initCore(ip: String, port: Int32, symKey: String, mtu: Int32, tuning: KcpTuningParams) -> Bool {
        vfsClient = zhiauth_create_vfs_client(
            ip, port, symKey, mtu,
            tuning.noDelay, tuning.interval, tuning.resend, tuning.nc,
            tuning.sndWnd, tuning.rcvWnd
        )
        guard let client = vfsClient else { return false }
        return zhiauth_start_vfs_client(client)
    }
    
    // Đổi tên thành stopCore cho đồng bộ
    public static func stopCore() {
        guard let client = vfsClient else { return }
        zhiauth_stop_vfs_client(client)
        zhiauth_destroy_vfs_client(client)
        vfsClient = nil
        
        Task {
            await requestManager.failAll(error: NSError(domain: "ZhiAuth", code: -500, userInfo: [NSLocalizedDescriptionKey: "KCP Engine Stopped"]))
        }
    }
    
    public static func sendRpcVfs(opcode: VfsOpcode, path: String, offset: UInt64, reqLen: UInt32, payloadData: Data?) async throws -> Data {
        guard let client = vfsClient else {
            throw NSError(domain: "ZhiAuth", code: -501, userInfo: [NSLocalizedDescriptionKey: "KCP Engine Not Running"])
        }
        
        currentSessionId &+= 1
        let reqId = currentSessionId
        
        return try await withCheckedThrowingContinuation { continuation in
            Task {
                await requestManager.addRequest(id: reqId, continuation: continuation)
                
                var cPayload: UnsafePointer<UInt8>? = nil
                var cPayloadLen: UInt32 = 0
                
                if let data = payloadData, !data.isEmpty {
                    data.withUnsafeBytes { ptr in
                        if let baseAddress = ptr.baseAddress {
                            cPayload = baseAddress.assumingMemoryBound(to: UInt8.self)
                            cPayloadLen = UInt32(data.count)
                        }
                    }
                }
                
                zhiauth_vfs_send_rpc(client, reqId, UInt8(opcode.rawValue), path, offset, reqLen, cPayload, cPayloadLen)
            }
        }
    }
}

@_cdecl("zhiauth_swift_on_response")
public func zhiauth_swift_on_response(reqId: UInt32, payload: UnsafePointer<UInt8>?, len: UInt32) {
    let data = payload != nil ? Data(bytes: payload!, count: Int(len)) : Data()
    Task {
        await ZhiKcpEngine.requestManager.completeRequest(id: reqId, data: data)
    }
}

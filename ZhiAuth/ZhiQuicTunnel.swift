import Foundation
import Network

public class ZhiQuicTunnel: @unchecked Sendable {
    private var connection: NWConnection?
    private let tunnelQueue = DispatchQueue(label: "com.zhiauth.quic_tunnel")
    private let tunnelLock = NSLock()
    private var isConnected = false
    private var readBuffer = Data()
    
    public init() {}
    
    public func connect(ip: String, port: UInt16) -> Bool {
        let host = NWEndpoint.Host(ip)
        guard let portEndpoint = NWEndpoint.Port(rawValue: port) else { return false }
        
        let options = NWProtocolQUIC.Options(alpn: ["zhiauth-rpc"])
        let secOptions = options.securityProtocolOptions
        sec_protocol_options_set_verify_block(secOptions, { _, _, sec_protocol_verify_complete in
            sec_protocol_verify_complete(true) 
        }, .main)
        
        let parameters = NWParameters(quic: options)
        connection = NWConnection(host: host, port: portEndpoint, using: parameters)
        
        let semaphore = DispatchSemaphore(value: 0)
        var connectSuccess = false
        
        connection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                self?.tunnelLock.lock()
                self?.isConnected = true
                self?.tunnelLock.unlock()
                self?.startReceiveLoop()
                connectSuccess = true
                semaphore.signal()
            case .failed(_), .cancelled:
                self?.tunnelLock.lock()
                self?.isConnected = false
                self?.tunnelLock.unlock()
                semaphore.signal()
            default:
                break
            }
        }
        
        connection?.start(queue: tunnelQueue)
        _ = semaphore.wait(timeout: .now() + 5.0)
        return connectSuccess
    }
    
    public func disconnect() {
        tunnelLock.lock()
        isConnected = false
        tunnelLock.unlock()
        connection?.cancel()
        connection = nil
    }
    
    public func sendData(_ data: Data) -> Bool {
        tunnelLock.lock()
        let active = isConnected
        tunnelLock.unlock()
        
        guard active, let conn = connection else { return false }
        
        var sendSuccess = false
        let semaphore = DispatchSemaphore(value: 0)
        
        conn.send(content: data, completion: .contentProcessed { error in
            if error == nil { sendSuccess = true }
            semaphore.signal()
        })
        
        _ = semaphore.wait(timeout: .now() + 5.0)
        return sendSuccess
    }
    
    private func startReceiveLoop() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] content, _, isComplete, error in
            guard let self = self else { return }
            
            if let content = content, !content.isEmpty {
                self.tunnelLock.lock()
                self.readBuffer.append(content)
                self.tunnelLock.unlock()
            }
            
            if error == nil && !isComplete {
                self.startReceiveLoop()
            } else {
                self.tunnelLock.lock()
                self.isConnected = false
                self.tunnelLock.unlock()
            }
        }
    }
    
    public func readAvailableData() -> Data {
        tunnelLock.lock()
        defer { tunnelLock.unlock() }
        let data = readBuffer
        readBuffer.removeAll(keepingCapacity: true)
        return data
    }
}

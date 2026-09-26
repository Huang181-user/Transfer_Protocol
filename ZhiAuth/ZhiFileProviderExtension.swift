import FileProvider
import Foundation
import UniformTypeIdentifiers
import UIKit

class ZhiFileProviderExtension: NSObject, NSFileProviderReplicatedExtension {
    var quicTunnel: ZhiQuicTunnel?
    var serverIP = "192.168.1.83"
    
    required init(domain: NSFileProviderDomain) {
        super.init()
        ZhiLogger.info("ZhiAuth FileProvider Extension initialized for domain: \(domain.identifier.rawValue)")
        
        Task { [self] in
            do {
                // Bọc MainActor.run để lấy HWID an toàn từ UIKit
                let hwid = await MainActor.run {
                    UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS"
                }
                
                let authCmd = "AUTH_REQ|USER:huang|PASS:123456|LAN:\(self.serverIP)|TS:NONE|HWID:\(hwid)"
                
                let auth = try await ZhiNetworkAuth.executePortKnockingAuth(ip: self.serverIP, authPort: 5555, authCmd: authCmd)
                
                if auth.isSuccess {
                    _ = ZhiKcpEngine.initCore(ip: self.serverIP, port: Int32(auth.kcpPort), symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", mtu: 1350, tuning: auth.tuning)
                    
                    self.quicTunnel = ZhiQuicTunnel(ip: self.serverIP, port: auth.quicPort)
                    self.quicTunnel?.connect()
                    
                    ZhiLogger.info("Dual-Tunnel (KCP + QUIC) is fully operational!")
                }
            } catch {
                ZhiLogger.error("Authentication/Tunnel ignition failed: \(error.localizedDescription)")
            }
        }
    }
    
    func invalidate() {
        ZhiLogger.warning("ZhiAuth FileProvider Extension invalidated.")
        ZhiKcpEngine.shutdownCore()
    }
    
    // 1. LẤY THÔNG TIN FILE (OP_STAT)
    func item(for identifier: NSFileProviderItemIdentifier, request: NSFileProviderRequest, completionHandler: @escaping (NSFileProviderItem?, Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 1)
        Task {
            do {
                let path = identifier.rawValue == NSFileProviderItemIdentifier.rootContainer.rawValue ? "/" : identifier.rawValue
                let statData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: path, offset: 0, reqLen: 0, payloadData: nil)
                
                if statData.count >= 37 {
                    let size = statData.subdata(in: 0..<8).withUnsafeBytes { $0.load(as: UInt64.self).littleEndian }
                    let isDir = statData[8] == 1
                    let item = ZhiFileItem(identifier: identifier, filename: (path as NSString).lastPathComponent, isDirectory: isDir, size: Int64(size))
                    completionHandler(item, nil)
                } else {
                    completionHandler(nil, NSFileProviderError(.noSuchItem))
                }
            } catch {
                ZhiLogger.error("Failed to stat item at path: \(identifier.rawValue) - Error: \(error.localizedDescription)")
                completionHandler(nil, error)
            }
        }
        return progress
    }
    
    // 2. TẢI NỘI DUNG FILE (OP_READ)
    func fetchContents(for itemIdentifier: NSFileProviderItemIdentifier, version requestedVersion: NSFileProviderItemVersion?, request: NSFileProviderRequest, completionHandler: @escaping (URL?, NSFileProviderItem?, Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 100)
        Task {
            do {
                let path = itemIdentifier.rawValue
                ZhiLogger.info("Fetching contents via KCP for file: \(path)")
                let statData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: path, offset: 0, reqLen: 0, payloadData: nil)
                var size: UInt64 = 0
                if statData.count >= 37 {
                    size = statData.subdata(in: 0..<8).withUnsafeBytes { $0.load(as: UInt64.self).littleEndian }
                }
                
                let fileData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_READ, path: path, offset: 0, reqLen: UInt32(size), payloadData: nil)
                let tempURL = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
                try fileData.write(to: tempURL)
                
                let item = ZhiFileItem(identifier: itemIdentifier, filename: (path as NSString).lastPathComponent, isDirectory: false, size: Int64(fileData.count))
                completionHandler(tempURL, item, nil)
            } catch {
                ZhiLogger.error("Fetch contents failed for \(itemIdentifier.rawValue) - Error: \(error.localizedDescription)")
                completionHandler(nil, nil, error)
            }
        }
        return progress
    }

    // 3. TẠO FILE/THƯ MỤC MỚI (OP_MKDIR / OP_WRITE)
    func createItem(basedOn itemTemplate: NSFileProviderItem, fields: NSFileProviderItemFields, contents url: URL?, options: NSFileProviderCreateItemOptions = [], request: NSFileProviderRequest, completionHandler: @escaping (NSFileProviderItem?, NSFileProviderItemFields, Bool, Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 1)
        Task {
            do {
                let path = itemTemplate.itemIdentifier.rawValue
                let isDir = itemTemplate.contentType == .folder
                let opcode: VfsOpcode = isDir ? .OP_MKDIR : .OP_WRITE
                var payloadData: Data? = nil
                if let url = url, !isDir {
                    payloadData = try Data(contentsOf: url)
                }
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: opcode, path: path, offset: 0, reqLen: 0, payloadData: payloadData)
                let createdItem = ZhiFileItem(identifier: itemTemplate.itemIdentifier, filename: itemTemplate.filename, isDirectory: isDir, size: Int64(payloadData?.count ?? 0))
                completionHandler(createdItem, [], false, nil)
            } catch {
                ZhiLogger.error("Create item failed: \(error.localizedDescription)")
                completionHandler(nil, [], false, error)
            }
        }
        return progress
    }

    // 4. SỬA FILE (OP_WRITE)
    func modifyItem(_ item: NSFileProviderItem, baseVersion: NSFileProviderItemVersion, changedFields: NSFileProviderItemFields, contents newContents: URL?, options: NSFileProviderModifyItemOptions = [], request: NSFileProviderRequest, completionHandler: @escaping (NSFileProviderItem?, NSFileProviderItemFields, Bool, Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 1)
        Task {
            do {
                let path = item.itemIdentifier.rawValue
                var payloadData: Data? = nil
                if let newContents = newContents {
                    payloadData = try Data(contentsOf: newContents)
                }
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_WRITE, path: path, offset: 0, reqLen: 0, payloadData: payloadData)
                completionHandler(item, [], false, nil)
            } catch {
                ZhiLogger.error("Modify item failed: \(error.localizedDescription)")
                completionHandler(nil, [], false, error)
            }
        }
        return progress
    }

    // 5. XÓA FILE/THƯ MỤC (OP_DELETE)
    func deleteItem(identifier: NSFileProviderItemIdentifier, baseVersion: NSFileProviderItemVersion, options: NSFileProviderDeleteItemOptions = [], request: NSFileProviderRequest, completionHandler: @escaping (Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 1)
        Task {
            do {
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_DELETE, path: identifier.rawValue, offset: 0, reqLen: 0, payloadData: nil)
                completionHandler(nil)
            } catch {
                ZhiLogger.error("Delete item failed: \(error.localizedDescription)")
                completionHandler(error)
            }
        }
        return progress
    }
    
    // 6. XEM DANH SÁCH THƯ MỤC
    func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier, request: NSFileProviderRequest) throws -> NSFileProviderEnumerator {
        return ZhiFileEnumerator(containerIdentifier: containerItemIdentifier)
    }
}

class ZhiFileItem: NSObject, NSFileProviderItem {
    var itemIdentifier: NSFileProviderItemIdentifier
    var parentItemIdentifier: NSFileProviderItemIdentifier
    var filename: String
    var contentType: UTType
    var documentSize: NSNumber?
    
    init(identifier: NSFileProviderItemIdentifier, filename: String, isDirectory: Bool, size: Int64) {
        self.itemIdentifier = identifier
        self.parentItemIdentifier = .rootContainer
        self.filename = filename
        self.contentType = isDirectory ? .folder : .data
        self.documentSize = NSNumber(value: size)
    }
}

class ZhiFileEnumerator: NSObject, NSFileProviderEnumerator {
    let containerIdentifier: NSFileProviderItemIdentifier
    init(containerIdentifier: NSFileProviderItemIdentifier) {
        self.containerIdentifier = containerIdentifier
    }
    func invalidate() {}
    
    func enumerateItems(for observer: NSFileProviderEnumerationObserver, startingAt page: NSFileProviderPage) {
        Task {
            do {
                let path = containerIdentifier.rawValue == NSFileProviderItemIdentifier.rootContainer.rawValue ? "/" : containerIdentifier.rawValue
                ZhiLogger.info("Enumerating items for folder: \(path)")
                let listData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_LIST, path: path, offset: 0, reqLen: 0, payloadData: nil)
                
                var items: [ZhiFileItem] = []
                var offset = 0
                let totalLen = listData.count
                
                while offset + 15 <= totalLen {
                    let nameLen = Int(listData.subdata(in: offset..<offset+2).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian })
                    let isDirVal = listData[offset + 2]
                    let size = listData.subdata(in: offset+3..<offset+11).withUnsafeBytes { $0.load(as: UInt64.self).littleEndian }
                    offset += 15
                    if offset + nameLen > totalLen { break }
                    
                    let nameData = listData.subdata(in: offset..<offset+nameLen)
                    let name = String(data: nameData, encoding: .utf8) ?? "Unknown"
                    offset += nameLen
                    
                    let itemID = NSFileProviderItemIdentifier(path == "/" ? "/\(name)" : "\(path)/\(name)")
                    let item = ZhiFileItem(identifier: itemID, filename: name, isDirectory: isDirVal == 1, size: Int64(size))
                    items.append(item)
                }
                observer.didEnumerate(items)
                observer.finishEnumerating(upTo: nil)
            } catch {
                ZhiLogger.error("Enumerate items failed: \(error.localizedDescription)")
                observer.finishEnumeratingWithError(error)
            }
        }
    }
}
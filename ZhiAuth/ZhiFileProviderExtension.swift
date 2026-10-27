import FileProvider
import UniformTypeIdentifiers

// Hàm ghi log thẳng vào ổ cứng dùng chung (App Group)
func extLog(_ msg: String) {
    guard let groupURL = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else { return }
    let logURL = groupURL.appendingPathComponent("ext_debug.log")
    let timestamp = Date().formatted(date: .omitted, time: .standard)
    let fullMsg = "[\(timestamp)] [EXTENSION] \(msg)\n"
    if let handle = try? FileHandle(forWritingTo: logURL) {
        handle.seekToEndOfFile()
        if let data = fullMsg.data(using: .utf8) { handle.write(data) }
        try? handle.close()
    } else {
        try? fullMsg.data(using: .utf8)?.write(to: logURL)
    }
}

class ZhiFileItem: NSObject, NSFileProviderItem {
    var itemIdentifier: NSFileProviderItemIdentifier
    var parentItemIdentifier: NSFileProviderItemIdentifier
    var filename: String
    var contentType: UTType
    var typeIdentifier: String { contentType.identifier }
    
    init(identifier: NSFileProviderItemIdentifier, parent: NSFileProviderItemIdentifier, filename: String, contentType: UTType) {
        self.itemIdentifier = identifier
        self.parentItemIdentifier = parent
        self.filename = filename
        self.contentType = contentType
    }
}

class ZhiFileEnumerator: NSObject, NSFileProviderEnumerator {
    func enumerateItems(for observer: NSFileProviderEnumerationObserver, startingAt page: NSFileProviderPage) {
        extLog("Đang liệt kê file trong thư mục...")
        observer.didEnumerate([])
        observer.finishEnumerating(upTo: nil)
    }
    func invalidate() {}
}

@objc(ZhiFileProviderExtension)
class ZhiFileProviderExtension: NSFileProviderExtension {
    
    override init() {
        super.init()
        extLog("🚀 Extension Init thành công! Tiến trình đã sống dậy.")
    }
    
    override func item(for identifier: NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
        extLog("Được yêu cầu item cho: \(identifier.rawValue)")
        if identifier == .rootContainer {
            return ZhiFileItem(identifier: .rootContainer, parent: .rootContainer, filename: "ZhiAuth Cloud", contentType: .folder)
        }
        throw NSError(domain: NSFileProviderErrorDomain, code: NSFileProviderError.noSuchItem.rawValue, userInfo: nil)
    }
    
    override func urlForItem(withPersistentIdentifier identifier: NSFileProviderItemIdentifier) -> URL? {
        guard let group = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else {
            extLog("❌ LỖI TRẦM TRỌNG: Không thể truy cập App Group Container!")
            return nil
        }
        let url = group.appendingPathComponent(identifier.rawValue)
        extLog("Yêu cầu URL cho \(identifier.rawValue) -> \(url.path)")
        return url
    }
    
    override func persistentIdentifierForItem(at url: URL) -> NSFileProviderItemIdentifier? {
        extLog("Yêu cầu ID cho URL: \(url.path)")
        return NSFileProviderItemIdentifier(url.lastPathComponent)
    }
    
    override func providePlaceholder(at url: URL, completionHandler: @escaping (Error?) -> Void) { completionHandler(nil) }
    override func startProvidingItem(at url: URL, completionHandler: @escaping (Error?) -> Void) { completionHandler(nil) }
    override func itemChanged(at url: URL) { }
    override func stopProvidingItem(at url: URL) { }
    
    override func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier) throws -> NSFileProviderEnumerator {
        extLog("Yêu cầu Enumerator cho: \(containerItemIdentifier.rawValue)")
        return ZhiFileEnumerator()
    }
}

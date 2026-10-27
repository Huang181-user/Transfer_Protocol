import FileProvider
import UniformTypeIdentifiers

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
        observer.didEnumerate([])
        observer.finishEnumerating(upTo: nil)
    }
    func invalidate() {}
}

class ZhiFileProviderExtension: NSFileProviderExtension {
    
    override func item(for identifier: NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
        // Bắt buộc trả về một thư mục giả lập hợp lệ cho Root Container
        if identifier == .rootContainer {
            return ZhiFileItem(identifier: .rootContainer, parent: .rootContainer, filename: "ZhiAuth Cloud", contentType: .folder)
        }
        throw NSError(domain: NSFileProviderErrorDomain, code: NSFileProviderError.noSuchItem.rawValue, userInfo: nil)
    }
    
    override func urlForItem(withPersistentIdentifier identifier: NSFileProviderItemIdentifier) -> URL? {
        guard let group = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.ruanhuang.ZhiAuth") else { return nil }
        return group.appendingPathComponent(identifier.rawValue)
    }
    
    override func persistentIdentifierForItem(at url: URL) -> NSFileProviderItemIdentifier? {
        return NSFileProviderItemIdentifier(url.lastPathComponent)
    }
    
    override func providePlaceholder(at url: URL, completionHandler: @escaping (Error?) -> Void) { completionHandler(nil) }
    
    override func startProvidingItem(at url: URL, completionHandler: @escaping (Error?) -> Void) { completionHandler(nil) }
    
    override func itemChanged(at url: URL) { }
    
    override func stopProvidingItem(at url: URL) { }
    
    override func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier) throws -> NSFileProviderEnumerator {
        return ZhiFileEnumerator()
    }
}

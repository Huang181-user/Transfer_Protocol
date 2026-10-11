import FileProvider

class ZhiFileProviderExtension: NSFileProviderExtension {
    override init() {
        super.init()
    }
    
    override func item(for identifier: NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
        return NSFileProviderItemIdentifier.rootContainer as! NSFileProviderItem
    }
    
    override func urlForItem(withPersistentIdentifier identifier: NSFileProviderItemIdentifier) -> URL? {
        return nil
    }
    
    override func persistentIdentifierForItem(at url: URL) -> NSFileProviderItemIdentifier? {
        return nil
    }
    
    override func providePlaceholder(at url: URL, completionHandler: @escaping (Error?) -> Void) {
        completionHandler(nil)
    }
    
    override func startProvidingItem(at url: URL, completionHandler: @escaping (Error?) -> Void) {
        completionHandler(nil)
    }
    
    override func itemChanged(at url: URL) { }
    override func stopProvidingItem(at url: URL) { }
    
    // Ghi đè phương thức CHUẨN XÁC, không có NSFileProviderRequest
    // Sử dụng thẳng NSError tự định nghĩa để né lỗi Enum
    override func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier) throws -> NSFileProviderEnumerator {
        let customError = NSError(
            domain: "com.zhiauth.fileprovider",
            code: 100,
            userInfo: [NSLocalizedDescriptionKey: "Tính năng duyệt file đang được phát triển."]
        )
        throw customError
    }
}
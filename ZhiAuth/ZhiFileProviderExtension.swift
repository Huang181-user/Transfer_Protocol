import FileProvider

class ZhiFileProviderExtension: NSFileProviderExtension {
    
    // Tạo 1 lỗi báo "Chưa hỗ trợ" để quăng vào các hàm bắt buộc, chiều lòng compiler
    var dummyError: NSError {
        return NSError(domain: "com.zhiauth.fileprovider", code: -1, userInfo: [NSLocalizedDescriptionKey: "Not implemented yet"])
    }

    override init() {
        super.init()
    }
    
    override func item(for identifier: NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
        // Đã gỡ bỏ đoạn ép kiểu (as!) sai nguyên lý. Quăng thẳng lỗi để qua ải compile.
        throw dummyError
    }
    
    override func urlForItem(withPersistentIdentifier identifier: NSFileProviderItemIdentifier) -> URL? {
        return nil
    }
    
    override func persistentIdentifierForItem(at url: URL) -> NSFileProviderItemIdentifier? {
        return nil
    }
    
    override func providePlaceholder(at url: URL, completionHandler: @escaping (Error?) -> Void) {
        completionHandler(dummyError)
    }
    
    override func startProvidingItem(at url: URL, completionHandler: @escaping (Error?) -> Void) {
        completionHandler(dummyError)
    }
    
    override func itemChanged(at url: URL) { }
    
    override func stopProvidingItem(at url: URL) { }
    
    override func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier) throws -> NSFileProviderEnumerator {
        throw dummyError
    }
}

import FileProvider

class ZhiFileProviderExtension: NSFileProviderExtension {
    
    var dummyError: NSError {
        return NSError(domain: "ZhiAuth.FileProvider", code: -1, userInfo: [NSLocalizedDescriptionKey: "Chưa hỗ trợ"])
    }

    override init() {
        super.init()
    }
    
    override func item(for identifier: NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
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

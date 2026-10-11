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
    
    override func enumerator(for containerItemIdentifier: NSFileProviderItemIdentifier, request: NSFileProviderRequest) throws -> NSFileProviderEnumerator {
        throw NSError(domain: NSFileProviderErrorDomain, code: NSFileProviderError.Code.notSupported.rawValue, userInfo: nil)
    }
}

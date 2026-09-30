import Foundation

enum LogLevel: String {
    case DEBUG = "DEBUG"
    case INFO = "INFO"
    case WARNING = "WARNING"
    case ERROR = "ERROR"
}

class ZhiLogger {
    private static let lock = NSLock()
    private static let dateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        return formatter
    }()
    
    static func log(level: LogLevel, file: String = #file, line: Int = #line, function: String = #function, message: String) {
        lock.lock()
        defer { lock.unlock() }
        
        let timestamp = dateFormatter.string(from: Date())
        let fileName = (file as NSString).lastPathComponent
        NSLog("%@", "[\(timestamp)] [\(level.rawValue)] [\(fileName):\(line)::\(function)] -> \(message)")
    }
    
    static func debug(_ message: String, file: String = #file, line: Int = #line, function: String = #function) {
        log(level: .DEBUG, file: file, line: line, function: function, message: message)
    }
    
    static func info(_ message: String, file: String = #file, line: Int = #line, function: String = #function) {
        log(level: .INFO, file: file, line: line, function: function, message: message)
    }
    
    static func warning(_ message: String, file: String = #file, line: Int = #line, function: String = #function) {
        log(level: .WARNING, file: file, line: line, function: function, message: message)
    }
    
    static func error(_ message: String, file: String = #file, line: Int = #line, function: String = #function) {
        log(level: .ERROR, file: file, line: line, function: function, message: message)
    }
}
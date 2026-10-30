import SwiftUI
import UIKit
import Darwin
import Network

func getDeviceIPs() -> (lan: String, ts: String) {
    var lan = ""; var ts = ""
    var ifaddr: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&ifaddr) == 0 else { return (lan, ts) }
    var ptr = ifaddr
    while ptr != nil {
        defer { ptr = ptr?.pointee.ifa_next }
        guard let interface = ptr?.pointee else { continue }
        let addrFamily = interface.ifa_addr.pointee.sa_family
        if addrFamily == UInt8(AF_INET) {
            let name = String(cString: interface.ifa_name)
            var hostname = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            getnameinfo(interface.ifa_addr, socklen_t(interface.ifa_addr.pointee.sa_len), &hostname, socklen_t(hostname.count), nil, socklen_t(0), NI_NUMERICHOST)
            let ip = String(cString: hostname)
            if ip.hasPrefix("127.") || ip.hasPrefix("169.254.") { continue }
            if name == "en0" || name == "pdp_ip0" { lan = ip } else if name.hasPrefix("utun") && ip.hasPrefix("100.") { ts = ip }
        }
    }
    freeifaddrs(ifaddr)
    return (lan, ts)
}

class AppState: ObservableObject {
    @Published var isLoggedIn: Bool = false
    @Published var currentPath: String = "/"
    @Published var fileList: [ZhiFileItem] = []
    @Published var isLoading: Bool = false
    @Published var logs: String = "💀 Sẵn sàng chọc thủng server...\n"
    @Published var transferSpeed: String = "0 KB/s"
    
    // Clipboard dùng cho tính năng Copy/Cut
    @Published var clipboardItem: ZhiFileItem?
    @Published var isCutOperation: Bool = false
    
    var sharedRoot: String = ""
    var baseIP: String = ""
    
    func log(_ msg: String) {
        DispatchQueue.main.async {
            let timestamp = Date().formatted(date: .omitted, time: .standard)
            self.logs += "[\(timestamp)] \(msg)\n"
            if self.logs.count > 4000 { self.logs = String(self.logs.suffix(3000)) }
        }
    }
}

// MARK: - Trình xử lý Xem trước file bằng Native QuickLook của iOS
class DocumentPresenter: NSObject, UIDocumentInteractionControllerDelegate {
    static let shared = DocumentPresenter()
    var documentController: UIDocumentInteractionController?

    func presentPreview(for url: URL) {
        DispatchQueue.main.async {
            self.documentController = UIDocumentInteractionController(url: url)
            self.documentController?.delegate = self
            
            // Ép hệ thống mở Native QuickLook (đọc ảnh, video, pdf, v.v.)
            let canPreview = self.documentController?.presentPreview(animated: true) ?? false
            
            // Nếu iOS KHÔNG hỗ trợ định dạng này -> Tự động rớt xuống mở popup Share mặc định
            if !canPreview {
                guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
                      let window = scene.windows.first,
                      let rootVC = window.rootViewController else { return }
                
                if UIDevice.current.userInterfaceIdiom == .pad {
                    self.documentController?.presentOptionsMenu(from: CGRect(x: UIScreen.main.bounds.midX, y: UIScreen.main.bounds.midY, width: 0, height: 0), in: rootVC.view, animated: true)
                } else {
                    self.documentController?.presentOptionsMenu(from: rootVC.view.bounds, in: rootVC.view, animated: true)
                }
            }
        }
    }
    
    func documentInteractionControllerViewControllerForPreview(_ controller: UIDocumentInteractionController) -> UIViewController {
        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let window = scene.windows.first,
              let rootVC = window.rootViewController else { return UIViewController() }
        return rootVC
    }
}

@main
struct ZhiAuthApp: App {
    @StateObject private var appState = AppState()
    var body: some Scene {
        WindowGroup {
            if appState.isLoggedIn {
                FileExplorerView().environmentObject(appState)
            } else {
                LoginView().environmentObject(appState)
            }
        }
    }
}

struct LoginView: View {
    @EnvironmentObject var state: AppState
    @AppStorage("savedLanIP") private var lanIP: String = "192.168.1.83"
    @AppStorage("savedTsIP") private var tsIP: String = ""
    @AppStorage("savedUser") private var username: String = "huang"
    @AppStorage("savedPass") private var password: String = ""
    
    var body: some View {
        VStack(spacing: 20) {
            Text("ZhiAuth v6.0").font(.largeTitle).bold().padding(.top, 40)
            
            VStack(spacing: 15) {
                TextField("LAN IP Server", text: $lanIP).textFieldStyle(RoundedBorderTextFieldStyle()).keyboardType(.numbersAndPunctuation).autocapitalization(.none)
                TextField("Tailscale IP Server", text: $tsIP).textFieldStyle(RoundedBorderTextFieldStyle()).keyboardType(.numbersAndPunctuation).autocapitalization(.none)
                TextField("Username", text: $username).textFieldStyle(RoundedBorderTextFieldStyle()).autocapitalization(.none)
                SecureField("Password", text: $password).textFieldStyle(RoundedBorderTextFieldStyle())
            }.padding(.horizontal, 30)
            
            if state.isLoading { ProgressView("Đang khởi động KCP Engine...").padding() }
            
            Button(action: performLogin) {
                Text("KẾT NỐI SERVER")
                    .font(.headline).foregroundColor(.white).padding()
                    .frame(maxWidth: .infinity).background(state.isLoading ? Color.gray : Color.blue).cornerRadius(10)
            }.disabled(state.isLoading).padding(.horizontal, 30)
            
            ScrollView {
                Text(state.logs).font(.system(size: 10, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading).padding()
            }.background(Color.black.opacity(0.8)).foregroundColor(.green).cornerRadius(10).padding()
        }.onReceive(NotificationCenter.default.publisher(for: NSNotification.Name("ZhiAuthLog"))) { note in
            if let msg = note.object as? String { state.log("C++ | " + msg) }
        }
    }
    
    func performLogin() {
        let safeLanIP = lanIP.trimmingCharacters(in: .whitespacesAndNewlines)
        let safeTsIP = tsIP.trimmingCharacters(in: .whitespacesAndNewlines)
        if safeLanIP.isEmpty && safeTsIP.isEmpty { state.log("❌ Lỗi: Phải nhập IP Server!"); return }
        
        state.isLoading = true
        state.log("Đang đua tốc độ LAN & Tailscale...")
        Task {
            do {
                let hwid = await MainActor.run { UIDevice.current.identifierForVendor?.uuidString ?? "UNKNOWN_IOS" }
                let myIPs = getDeviceIPs()
                let authCmd = "AUTH_REQ|USER:\(username)|PASS:\(password)|LAN:\(myIPs.lan.isEmpty ? "N/A" : myIPs.lan)|TS:\(myIPs.ts.isEmpty ? "N/A" : myIPs.ts)|HWID:\(hwid)"
                
                let winner: (String, AuthResponse)? = await withTaskGroup(of: Optional<(String, AuthResponse)>.self) { group in
                    if !safeLanIP.isEmpty { group.addTask { do { return (safeLanIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeLanIP, authPort: 5555, authCmd: authCmd)) } catch { return nil } } }
                    if !safeTsIP.isEmpty { group.addTask { do { return (safeTsIP, try await ZhiNetworkAuth.executePortKnockingAuth(ip: safeTsIP, authPort: 5555, authCmd: authCmd)) } catch { return nil } } }
                    for await result in group { if let res = result, res.1.isSuccess { group.cancelAll(); return res } }
                    return nil
                }
                
                guard let (winningIP, auth) = winner else { state.log("❌ Lỗi mạng: Knocking Timeout!"); state.isLoading = false; return }
                state.log("✅ Auth OK qua [\(winningIP)]! KCP Port: \(auth.kcpPort)")
                
                if ZhiKcpEngine.initCore(ip: winningIP, port: Int32(auth.kcpPort), symKey: "ZhiAuth_Secret_KCP_Key_2026_1234", mtu: 1200, tuning: auth.tuning) {
                    state.log("🔥 Lõi KCP đã nổ máy!")
                    try await Task.sleep(nanoseconds: 1_000_000_000)
                    state.sharedRoot = auth.sharedPath.isEmpty ? "/tmp" : auth.sharedPath
                    state.baseIP = winningIP
                    
                    _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: state.sharedRoot, offset: 0, reqLen: 0, payloadData: nil)
                    await MainActor.run { state.currentPath = state.sharedRoot; state.isLoggedIn = true; state.isLoading = false }
                } else { state.log("❌ Khởi động KCP Engine thất bại."); state.isLoading = false }
            } catch { state.log("❌ Lỗi: \(error.localizedDescription)"); state.isLoading = false }
        }
    }
}

struct FileExplorerView: View {
    @EnvironmentObject var state: AppState
    
    // States cho các Popup tính năng mới
    @State private var showNewFolderAlert = false
    @State private var newFolderName = ""
    
    @State private var showRenameAlert = false
    @State private var newName = ""
    @State private var itemToRename: ZhiFileItem?
    
    @State private var showDeleteAlert = false
    @State private var itemToDelete: ZhiFileItem?
    
    @State private var showInfoAlert = false
    @State private var infoMessage = ""

    var body: some View {
        NavigationView {
            VStack {
                HStack {
                    Text("Tốc độ: \(state.transferSpeed)").font(.caption).foregroundColor(.gray)
                    Spacer()
                    Text(state.currentPath).font(.caption).lineLimit(1).truncationMode(.middle)
                }.padding(.horizontal)
                
                List(state.fileList, id: \.id) { item in
                    HStack {
                        Image(systemName: item.isDirectory ? "folder.fill" : "doc.text.fill").foregroundColor(item.isDirectory ? .blue : .gray)
                        VStack(alignment: .leading) {
                            Text(item.name).font(.body)
                            Text(item.sizeString).font(.caption).foregroundColor(.secondary)
                        }
                    }
                    .contentShape(Rectangle())
                    .onTapGesture {
                        if item.isDirectory {
                            let newPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(item.name)" : "\(state.currentPath)/\(item.name)"
                            loadDirectory(path: newPath)
                        } else {
                            downloadAndShareFile(item: item)
                        }
                    }
                    .contextMenu {
                        Button(action: { showFileInfo(item) }) { Label("Thông tin", systemImage: "info.circle") }
                        Button(action: { state.clipboardItem = item; state.isCutOperation = false; state.log("Đã copy '\(item.name)' vào bộ nhớ tạm") }) { Label("Copy", systemImage: "doc.on.doc") }
                        Button(action: { state.clipboardItem = item; state.isCutOperation = true; state.log("Đã cắt '\(item.name)' vào bộ nhớ tạm") }) { Label("Cut", systemImage: "scissors") }
                        Button(action: { itemToRename = item; newName = item.name; showRenameAlert = true }) { Label("Đổi tên", systemImage: "pencil") }
                        Button(role: .destructive, action: { itemToDelete = item; showDeleteAlert = true }) { Label("Xóa", systemImage: "trash") }
                    }
                }
                .refreshable { await refreshList() }
                .overlay(Group {
                    if state.isLoading {
                        ProgressView("Đang tải...").padding().background(Color(UIColor.systemBackground).opacity(0.8)).cornerRadius(10)
                    } else if state.fileList.isEmpty {
                        Text("Thư mục rỗng").foregroundColor(.gray)
                    }
                })
            }
            .navigationTitle("ZhiAuth Explorer")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    if state.currentPath != state.sharedRoot {
                        Button(action: goUp) { HStack { Image(systemName: "chevron.backward"); Text("Quay lại") } }
                    }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    HStack(spacing: 15) {
                        if state.clipboardItem != nil {
                            Button(action: pasteItem) { Image(systemName: "doc.on.clipboard").foregroundColor(.green) }
                        }
                        Button(action: { newFolderName = ""; showNewFolderAlert = true }) { Image(systemName: "folder.badge.plus").foregroundColor(.blue) }
                        Button(action: logout) { Image(systemName: "power").foregroundColor(.red) }
                    }
                }
            }
            .onAppear { loadDirectory(path: state.currentPath) }
            
            // --- CÁC POPUP THAO TÁC FILE ---
            .alert("Tạo thư mục mới", isPresented: $showNewFolderAlert) {
                TextField("Tên thư mục", text: $newFolderName)
                Button("Tạo", action: createFolder)
                Button("Hủy", role: .cancel) { }
            }
            .alert("Đổi tên", isPresented: $showRenameAlert) {
                TextField("Tên mới", text: $newName)
                Button("Lưu", action: renameItem)
                Button("Hủy", role: .cancel) { }
            }
            .alert("Cảnh báo Xóa", isPresented: $showDeleteAlert) {
                Button("Xóa ngay", role: .destructive, action: deleteItem)
                Button("Hủy", role: .cancel) { }
            } message: {
                Text("Xóa file/thư mục này là vĩnh viễn, không thể phục hồi. Bạn có chắc không?")
            }
            .alert("Thông tin", isPresented: $showInfoAlert) {
                Button("Đóng", role: .cancel) { }
            } message: { Text(infoMessage) }
        }
    }
    
    // MARK: - CÁC HÀM THAO TÁC FILE CỐT LÕI
    
    func createFolder() {
        let cleanName = newFolderName.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleanName.isEmpty { return }
        let targetPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(cleanName)" : "\(state.currentPath)/\(cleanName)"
        state.log("Đang tạo thư mục: \(cleanName)...")
        Task {
            do {
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_MKDIR, path: targetPath, offset: 0, reqLen: 0, payloadData: nil)
                await refreshList()
            } catch { state.log("❌ Lỗi tạo thư mục: \(error.localizedDescription)") }
        }
    }
    
    func deleteItem() {
        guard let item = itemToDelete else { return }
        state.log("Đang xóa: \(item.name)...")
        Task {
            do {
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_DELETE, path: item.path, offset: 0, reqLen: 0, payloadData: nil)
                await refreshList()
                if state.clipboardItem?.id == item.id { await MainActor.run { state.clipboardItem = nil } }
            } catch { state.log("❌ Lỗi xóa: \(error.localizedDescription)") }
        }
    }
    
    func renameItem() {
        guard let item = itemToRename else { return }
        let cleanName = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleanName.isEmpty || cleanName == item.name { return }
        
        let url = URL(fileURLWithPath: item.path)
        let newPath = url.deletingLastPathComponent().appendingPathComponent(cleanName).path
        
        state.log("Đang đổi tên thành: \(cleanName)...")
        Task {
            do {
                // 🔥 FIX LỖI RENANME/CUT: Ép cứng NULL-TERMINATOR (0) vào đuôi để C++ đọc an toàn
                var newPathData = newPath.data(using: .utf8)!
                newPathData.append(0)
                
                _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_RENAME, path: item.path, offset: 0, reqLen: UInt32(newPathData.count), payloadData: newPathData)
                await refreshList()
                if state.clipboardItem?.id == item.id { await MainActor.run { state.clipboardItem = nil } }
            } catch { state.log("❌ Lỗi đổi tên: \(error.localizedDescription)") }
        }
    }
    
    func pasteItem() {
        guard let item = state.clipboardItem else { return }
        let destPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(item.name)" : "\(state.currentPath)/\(item.name)"
        
        if state.isCutOperation {
            state.log("Đang di chuyển (Cut) file...")
            Task {
                do {
                    // 🔥 FIX LỖI RENANME/CUT: Ép cứng NULL-TERMINATOR (0) vào đuôi
                    var newPathData = destPath.data(using: .utf8)!
                    newPathData.append(0)
                    
                    _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_RENAME, path: item.path, offset: 0, reqLen: UInt32(newPathData.count), payloadData: newPathData)
                    await MainActor.run { state.clipboardItem = nil; state.isCutOperation = false }
                    await refreshList()
                } catch { state.log("❌ Lỗi Cut: \(error.localizedDescription)") }
            }
        } else {
            if item.isDirectory {
                state.log("❌ Server chưa hỗ trợ Copy cả Thư mục (Bạn có thể Cut hoặc Copy từng file lẻ).")
                return
            }
            state.log("⏳ Đang Copy (Download & Upload tốc độ cao qua KCP)...")
            state.isLoading = true
            Task {
                do {
                    var offset: UInt64 = 0
                    let chunkSize: UInt32 = 65536 // 64KB per chunk
                    
                    // Gửi lệnh tạo file rỗng trước
                    _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_WRITE, path: destPath, offset: 0, reqLen: 0, payloadData: Data())
                    
                    while offset < item.size {
                        let remain = UInt32(min(UInt64(chunkSize), item.size - offset))
                        let readData = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_READ, path: item.path, offset: offset, reqLen: remain, payloadData: nil)
                        
                        if readData.count > 27 { // Header VFS dài 27 byte
                            let chunk = readData.subdata(in: 27..<readData.count)
                            _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_WRITE, path: destPath, offset: offset, reqLen: UInt32(chunk.count), payloadData: chunk)
                            offset += UInt64(chunk.count)
                        } else { break }
                    }
                    state.log("✅ Copy hoàn tất: \(item.name)")
                    await MainActor.run { state.clipboardItem = nil }
                    await refreshList()
                } catch {
                    state.log("❌ Lỗi Copy: \(error.localizedDescription)")
                }
                await MainActor.run { state.isLoading = false }
            }
        }
    }
    
    func showFileInfo(_ item: ZhiFileItem) {
        let date = Date(timeIntervalSince1970: TimeInterval(item.modifiedTime))
        let formatter = DateFormatter()
        formatter.dateFormat = "dd/MM/yyyy HH:mm:ss"
        
        infoMessage = """
        Tên: \(item.name)
        Loại: \(item.isDirectory ? "Thư mục" : "Tập tin")
        Dung lượng: \(item.isDirectory ? "Không đếm" : item.sizeString)
        Ngày sửa: \(formatter.string(from: date))
        Đường dẫn: \(item.path)
        """
        showInfoAlert = true
    }
    
    // MARK: - LOGIC ĐIỀU HƯỚNG VÀ DANH SÁCH
    
    func goUp() {
        var components = state.currentPath.split(separator: "/").map(String.init)
        if components.count > 1 {
            components.removeLast()
            let parentPath = "/" + components.joined(separator: "/")
            loadDirectory(path: parentPath)
        } else {
            loadDirectory(path: "/")
        }
    }
    
    func loadDirectory(path: String) {
        state.isLoading = true
        state.currentPath = path
        Task {
            do {
                let data = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_LIST, path: path, offset: 0, reqLen: 0, payloadData: nil)
                let parsedList = parseList(data: data, basePath: path)
                await MainActor.run {
                    state.fileList = parsedList.sorted {
                        if $0.isDirectory == $1.isDirectory { return $0.name.localizedStandardCompare($1.name) == .orderedAscending }
                        return $0.isDirectory
                    }
                    state.isLoading = false
                }
            } catch {
                await MainActor.run { state.log("❌ Lỗi load thư mục: \(error.localizedDescription)"); state.isLoading = false }
            }
        }
    }
    
    func downloadAndShareFile(item: ZhiFileItem) {
        state.isLoading = true
        state.log("⏳ Đang tải file \(item.name) qua KCP...")
        Task {
            do {
                // Tải file xuống thư mục tạm với ĐÚNG TÊN VÀ ĐUÔI GỐC để QuickLook nhận dạng được
                let tempURL = FileManager.default.temporaryDirectory.appendingPathComponent(item.name)
                if FileManager.default.fileExists(atPath: tempURL.path) { try FileManager.default.removeItem(at: tempURL) }
                FileManager.default.createFile(atPath: tempURL.path, contents: nil)
                let fileHandle = try FileHandle(forWritingTo: tempURL)
                defer { try? fileHandle.close() }
                
                var offset: UInt64 = 0
                let chunkSize: UInt32 = 65536 // Chunk 64KB
                
                while offset < item.size {
                    let remain = UInt32(min(UInt64(chunkSize), item.size - offset))
                    let data = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_READ, path: item.path, offset: offset, reqLen: remain, payloadData: nil)
                    
                    if data.count > 27 {
                        let fileData = data.subdata(in: 27..<data.count)
                        try fileHandle.seekToEnd()
                        try fileHandle.write(contentsOf: fileData)
                        offset += UInt64(fileData.count)
                    } else { throw NSError(domain: "ZhiAuth", code: -1, userInfo: [NSLocalizedDescriptionKey: "Dữ liệu rỗng từ server"]) }
                }
                
                await MainActor.run {
                    state.isLoading = false
                    state.log("✅ Tải xong! Bật xem trước QuickLook...")
                    // 🔥 SỬ DỤNG TRÌNH XEM TRƯỚC HỆ THỐNG UIDocumentInteractionController
                    DocumentPresenter.shared.presentPreview(for: tempURL)
                }
            } catch {
                await MainActor.run { state.isLoading = false; state.log("❌ Lỗi tải file: \(error.localizedDescription)") }
            }
        }
    }
    
    func refreshList() async { loadDirectory(path: state.currentPath) }
    
    func logout() {
        ZhiKcpEngine.stopCore()
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(state.baseIP), port: 5555)
        let conn = NWConnection(to: endpoint, using: .udp)
        conn.start(queue: .global())
        conn.send(content: "AUTH_REQ|LOGOUT".data(using: .utf8)!, completion: .contentProcessed { _ in conn.cancel() })
        
        state.isLoggedIn = false
        state.fileList = []
        state.currentPath = "/"
        state.clipboardItem = nil
        state.log("🛑 Đã đăng xuất và ngắt lõi KCP.")
    }
    
    func parseList(data: Data, basePath: String) -> [ZhiFileItem] {
        var result: [ZhiFileItem] = []
        let headerSize = 27
        if data.count <= headerSize { return result }
        
        var cur = headerSize
        while cur + 39 <= data.count {
            let nameLen = UInt16(data[cur]) | (UInt16(data[cur+1]) << 8)
            let isDir = data[cur+2] == 1
            var size: UInt64 = 0
            var mtime: UInt64 = 0
            withUnsafeMutableBytes(of: &size) { _ = data.copyBytes(to: $0, from: cur+3..<cur+11) }
            withUnsafeMutableBytes(of: &mtime) { _ = data.copyBytes(to: $0, from: cur+11..<cur+19) }
            
            cur += 39
            if cur + Int(nameLen) > data.count { break }
            
            if let name = String(data: data[cur..<cur+Int(nameLen)], encoding: .utf8) {
                if name != "." && name != ".." {
                    let fullPath = basePath.hasSuffix("/") ? basePath + name : basePath + "/" + name
                    result.append(ZhiFileItem(name: name, isDirectory: isDir, size: size, modifiedTime: mtime, path: fullPath))
                }
            }
            cur += Int(nameLen)
        }
        return result
    }
}

import SwiftUI
import Network

struct FileExplorerView: View {
    @EnvironmentObject var state: AppState
    
    @State private var showNewFolderAlert = false
    @State private var newFolderName = ""
    
    @State private var showRenameAlert = false
    @State private var newName = ""
    @State private var itemToRename: ZhiFileItem?
    
    @State private var showDeleteAlert = false
    @State private var itemToDelete: ZhiFileItem?
    
    @State private var showInfoAlert = false
    @State private var infoMessage = ""
    
    @State private var showFileImporter = false
    @State private var showLogSheet = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                compactHeaderBar
                statusBarView
                Divider()
                fileListContentView
                if !state.pendingSharedURLs.isEmpty {
                    Divider()
                    sharedFilesDestinationBar
                }
            }
            .toolbar(.hidden, for: .navigationBar)
            .task {
                if state.fileList.isEmpty {
                    loadDirectory(path: state.currentPath) }
                }
            }
            .sheet(isPresented: $showFileImporter) {
                LocalFilePicker { pickedURLs in
                    uploadFilesFromDevice(urls: pickedURLs)
                }
            }
            .sheet(isPresented: $showLogSheet) {
                logConsoleSheetView
            }
            .alert("Tạo thư mục mới", isPresented: $showNewFolderAlert) {
                TextField("Tên thư mục", text: $newFolderName)
                Button("Tạo", action: createFolder)
                Button("Hủy", role: .cancel) {}
            }
            .alert("Đổi tên", isPresented: $showRenameAlert) {
                TextField("Tên mới", text: $newName)
                Button("Lưu", action: renameItem)
                Button("Hủy", role: .cancel) {}
            }
            .alert("Xác nhận Xóa", isPresented: $showDeleteAlert) {
                Button("Xóa vĩnh viễn", role: .destructive, action: deleteItem)
                Button("Hủy", role: .cancel) {}
            } message: {
                Text("Bạn có chắc chắn muốn xóa vĩnh viễn mục này không?")
            }
            .alert("Thông tin Tập tin", isPresented: $showInfoAlert) {
                Button("Đóng", role: .cancel) {}
            } message: {
                Text(infoMessage)
            }
        }
    }
    
    private var compactHeaderBar: some View {
        HStack(spacing: 12) {
            if state.currentPath != "/" {
                Button(action: goUp) {
                    HStack(spacing: 4) {
                        Image(systemName: "chevron.backward")
                            .font(.system(size: 14, weight: .semibold))
                        Text("Lùi lại")
                            .font(.subheadline.weight(.medium))
                    }
                    .foregroundColor(.blue)
                }
            } else {
                Image(systemName: "externaldrive.fill.badge.wifi")
                    .font(.system(size: 16))
                    .foregroundColor(.blue)
            }
            
            Spacer()
            
            Text(state.currentFolderTitle)
                .font(.subheadline.weight(.semibold))
                .foregroundColor(.primary)
                .lineLimit(1)
            
            Spacer()
            
            HStack(spacing: 14) {
                if state.currentPath != "/" {
                    if state.clipboardItem != nil {
                        Button(action: pasteItem) {
                            Image(systemName: "doc.on.clipboard.fill")
                                .font(.system(size: 15))
                                .foregroundColor(.green)
                        }
                    }
                    Button(action: { showFileImporter = true }) {
                        Image(systemName: "plus")
                            .font(.system(size: 15))
                            .foregroundColor(.blue)
                    }
                    Button(action: { newFolderName = ""; showNewFolderAlert = true }) {
                        Image(systemName: "folder.badge.plus")
                            .font(.system(size: 15))
                            .foregroundColor(.blue)
                    }
                }
                Button(action: { showLogSheet = true }) {
                    Image(systemName: "terminal")
                        .font(.system(size: 15))
                        .foregroundColor(.primary)
                }
                Button(action: logout) {
                    Image(systemName: "power")
                        .font(.system(size: 15))
                        .foregroundColor(.red)
                }
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(Color(UIColor.systemBackground))
    }
    
    private var statusBarView: some View {
        HStack {
            HStack(spacing: 5) {
                Circle()
                    .fill(state.activeTunnel == .kcp ? Color.blue : (state.activeTunnel == .quic ? Color.orange : Color.gray))
                    .frame(width: 6, height: 6)
                Text("\(state.activeTunnel.rawValue) | \(state.transferSpeed)")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
                    .foregroundColor(state.activeTunnel == .kcp ? .blue : (state.activeTunnel == .quic ? .orange : .secondary))
            }
            Spacer()
            Text(state.displayPath(for: state.currentPath))
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.head)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        .background(Color(UIColor.secondarySystemBackground))
    }
    
    private var sharedFilesDestinationBar: some View {
        VStack(spacing: 8) {
            HStack {
                Image(systemName: "tray.and.arrow.down.fill")
                    .foregroundColor(.blue)
                Text(state.pendingSharedURLs.count == 1
                     ? "Tệp chờ lưu: \(state.pendingSharedURLs[0].lastPathComponent)"
                     : "Đang chờ lưu \(state.pendingSharedURLs.count) tệp")
                    .font(.caption.weight(.semibold))
                    .lineLimit(1)
                Spacer()
            }
            
            HStack(spacing: 10) {
                Button(role: .destructive, action: {
                    state.clearPendingSharedFiles()
                }) {
                    Text("Hủy bỏ")
                        .font(.caption.weight(.semibold))
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(Color.red.opacity(0.15))
                        .foregroundColor(.red)
                        .cornerRadius(8)
                }
                
                Button(action: {
                    let urls = state.pendingSharedURLs
                    uploadFilesFromDevice(urls: urls, clearPendingWhenDone: true)
                }) {
                    Text(state.currentPath == "/"
                         ? "Hãy mở KCP/QUIC để chọn nơi lưu"
                         : "Lưu vào \(state.displayPath(for: state.currentPath))")
                        .font(.caption.weight(.semibold))
                        .lineLimit(1)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(state.currentPath == "/" ? Color.gray.opacity(0.3) : Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                }
                .disabled(state.currentPath == "/" || state.isLoading)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(Color(UIColor.secondarySystemBackground))
    }
    
    private var fileListContentView: some View {
        List(state.fileList, id: \.id) { item in
            FileRowView(
                item: item,
                isRootTunnelMenu: state.currentPath == "/",
                onTap: { handleItemTap(item) },
                onInfo: { showFileInfo(item) },
                onCopy: { handleCopy(item) },
                onCut: { handleCut(item) },
                onRename: {
                    itemToRename = item
                    newName = item.name
                    showRenameAlert = true
                },
                onSaveToDevice: { downloadFile(item: item, openNativePreview: false) },
                onDelete: {
                    itemToDelete = item
                    showDeleteAlert = true
                }
            )
            .listRowSeparator(.hidden)
            .listRowInsets(EdgeInsets(top: 2, leading: 10, bottom: 2, trailing: 10))
        }
        .listStyle(.plain)
        .refreshable {
            await refreshList()
        }
        .overlay(loadingOverlayView)
    }
    
    @ViewBuilder
    private var loadingOverlayView: some View {
        if state.isLoading {
            VStack(spacing: 10) {
                ProgressView()
                Text("Đang xử lý (\(state.transferSpeed))...")
                    .font(.caption.bold())
            }
            .padding(14)
            .background(.ultraThinMaterial)
            .cornerRadius(10)
        } else if state.fileList.isEmpty {
            VStack(spacing: 6) {
                Image(systemName: "folder")
                    .font(.title2)
                    .foregroundColor(.secondary)
                Text("Thư mục trống")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }
    
    private var logConsoleSheetView: some View {
        NavigationStack {
            ScrollView {
                Text(state.logs)
                    .font(.system(size: 11, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
            }
            .background(Color.black)
            .foregroundColor(.green)
            .navigationTitle("Real-time Debug Logs")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Đóng") { showLogSheet = false }
                }
            }
        }
    }
    
    private func handleItemTap(_ item: ZhiFileItem) {
        if state.currentPath == "/" {
            if item.name == "ZhiAuth_KCP" {
                state.activeTunnel = .kcp
                state.log("🔵 Chuyển sang duyệt file qua luồng chính ZhiAuth_KCP")
                loadDirectory(path: state.sharedRoot)
            } else if item.name == "ZhiAuth_QUIC" {
                state.activeTunnel = .quic
                state.log("🟠 Chuyển sang duyệt file qua luồng dự phòng ZhiAuth_QUIC")
                loadDirectory(path: state.sharedRoot)
            }
        } else {
            if item.isDirectory {
                let nextPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(item.name)" : "\(state.currentPath)/\(item.name)"
                loadDirectory(path: nextPath)
            } else {
                downloadFile(item: item, openNativePreview: true)
            }
        }
    }
    
    private func handleCopy(_ item: ZhiFileItem) {
        state.clipboardItem = item
        state.isCutOperation = false
        state.log("📋 Đã Copy '\(item.name)' vào bộ nhớ tạm (\(state.activeTunnel.rawValue))")
    }
    
    private func handleCut(_ item: ZhiFileItem) {
        state.clipboardItem = item
        state.isCutOperation = true
        state.log("✂️ Đã Cut '\(item.name)' vào bộ nhớ tạm (\(state.activeTunnel.rawValue))")
    }
    
    func goUp() {
        if state.currentPath == state.sharedRoot {
            loadDirectory(path: "/")
        } else {
            let parentURL = URL(fileURLWithPath: state.currentPath).deletingLastPathComponent()
            let parentPath = parentURL.path
            if parentPath.count < state.sharedRoot.count || !parentPath.hasPrefix(state.sharedRoot) {
                loadDirectory(path: state.sharedRoot)
            } else {
                loadDirectory(path: parentPath)
            }
        }
    }
    
    func loadDirectory(path: String) {
        state.isLoading = true
        state.currentPath = path
        Task {
            if path == "/" {
                await MainActor.run {
                    state.fileList = [
                        ZhiFileItem(name: "ZhiAuth_KCP", isDirectory: true, size: 0, modifiedTime: 0, path: state.sharedRoot),
                        ZhiFileItem(name: "ZhiAuth_QUIC", isDirectory: true, size: 0, modifiedTime: 0, path: state.sharedRoot)
                    ]
                    state.activeTunnel = .none
                    state.isLoading = false
                }
                return
            }
            
            do {
                let data = try await state.dispatchRpc(opcode: .OP_LIST, path: path, offset: 0, reqLen: 0, payloadData: nil)
                let parsedList = parseList(data: data, basePath: path)
                await MainActor.run {
                    state.fileList = parsedList.sorted {
                        if $0.isDirectory == $1.isDirectory {
                            return $0.name.localizedStandardCompare($1.name) == .orderedAscending
                        }
                        return $0.isDirectory
                    }
                    state.isLoading = false
                }
            } catch {
                await MainActor.run {
                    state.log("❌ Lỗi đọc thư mục: \(error.localizedDescription)")
                    state.isLoading = false
                }
            }
        }
    }
    
    func refreshList() async {
        loadDirectory(path: state.currentPath) }
    }
    
    func createFolder() {
        let cleanName = newFolderName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanName.isEmpty else { return }
        let targetPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(cleanName)" : "\(state.currentPath)/\(cleanName)"
        state.log("📁 Đang tạo thư mục: \(cleanName)")
        Task {
            do {
                _ = try await state.dispatchRpc(opcode: .OP_MKDIR, path: targetPath, offset: 0, reqLen: 0, payloadData: nil)
                state.log("✅ Tạo thư mục '\(cleanName)' thành công!")
                await refreshList()
            } catch {
                state.log("❌ Lỗi tạo thư mục: \(error.localizedDescription)")
            }
        }
    }
    
    func deleteItem() {
        guard let item = itemToDelete else { return }
        state.log("🗑️ Đang xóa: \(item.name)")
        Task {
            do {
                _ = try await state.dispatchRpc(opcode: .OP_DELETE, path: item.path, offset: 0, reqLen: 0, payloadData: nil)
                state.log("✅ Đã xóa '\(item.name)'")
                if state.clipboardItem?.path == item.path {
                    await MainActor.run { state.clipboardItem = nil }
                }
                await refreshList()
            } catch {
                state.log("❌ Lỗi xóa: \(error.localizedDescription)")
            }
        }
    }
    
    func renameItem() {
        guard let item = itemToRename else { return }
        let cleanName = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanName.isEmpty, cleanName != item.name else { return }
        
        let parentPath = URL(fileURLWithPath: item.path).deletingLastPathComponent().path
        let newPath = parentPath.hasSuffix("/") ? "\(parentPath)\(cleanName)" : "\(parentPath)/\(cleanName)"
        let newPathData = Data(newPath.utf8)
        
        state.log("✏️ Đang đổi tên '\(item.name)' -> '\(cleanName)'")
        Task {
            do {
                _ = try await state.dispatchRpc(
                    opcode: .OP_RENAME,
                    path: item.path,
                    offset: 0,
                    reqLen: UInt32(newPathData.count),
                    payloadData: newPathData
                )
                state.log("✅ Đổi tên thành công: \(cleanName)")
                if state.clipboardItem?.path == item.path {
                    await MainActor.run { state.clipboardItem = nil }
                }
                await refreshList()
            } catch {
                state.log("❌ Lỗi đổi tên: \(error.localizedDescription)")
            }
        }
    }
    
    func pasteItem() {
        guard let item = state.clipboardItem else { return }
        let destPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(item.name)" : "\(state.currentPath)/\(item.name)"
        
        if state.isCutOperation {
            let newPathData = Data(destPath.utf8)
            state.log("✂️ Đang di chuyển '\(item.name)'...")
            Task {
                do {
                    _ = try await state.dispatchRpc(
                        opcode: .OP_RENAME,
                        path: item.path,
                        offset: 0,
                        reqLen: UInt32(newPathData.count),
                        payloadData: newPathData
                    )
                    state.log("✅ Di chuyển hoàn tất: \(item.name)")
                    await MainActor.run {
                        state.clipboardItem = nil
                        state.isCutOperation = false
                    }
                    await refreshList()
                } catch {
                    state.log("❌ Lỗi di chuyển: \(error.localizedDescription)")
                }
            }
        } else {
            if item.isDirectory {
                state.log("⚠️ Để di chuyển cả thư mục, vui lòng dùng tính năng Cut.")
                return
            }
            state.isLoading = true
            state.log("📋 Đang sao chép '\(item.name)' qua luồng \(state.activeTunnel.rawValue)...")
            Task {
                do {
                    let startTotal = CFAbsoluteTimeGetCurrent()
                    _ = try await state.dispatchRpc(opcode: .OP_TRUNCATE, path: destPath, offset: 0, reqLen: 0, payloadData: nil)
                    
                    var offset: UInt64 = 0
                    let chunkSize: UInt32 = 32768
                    while offset < item.size {
                        let remain = UInt32(min(UInt64(chunkSize), item.size - offset))
                        let readResp = try await state.dispatchRpc(opcode: .OP_READ, path: item.path, offset: offset, reqLen: remain, payloadData: nil)
                        guard readResp.count > 27 else { break }
                        let chunk = readResp.subdata(in: 27..<readResp.count)
                        _ = try await state.dispatchRpc(opcode: .OP_WRITE, path: destPath, offset: offset, reqLen: UInt32(chunk.count), payloadData: chunk)
                        offset += UInt64(chunk.count)
                    }
                    let totalElapsed = CFAbsoluteTimeGetCurrent() - startTotal
                    state.updateSpeed(bytes: Int(item.size) * 2, durationSeconds: totalElapsed)
                    state.log("✅ Sao chép hoàn tất: \(item.name)")
                    await MainActor.run {
                        state.clipboardItem = nil
                        state.isLoading = false
                    }
                    await refreshList()
                } catch {
                    state.log("❌ Lỗi sao chép: \(error.localizedDescription)")
                    await MainActor.run { state.isLoading = false }
                }
            }
        }
    }
    
    func downloadFile(item: ZhiFileItem, openNativePreview: Bool) {
        state.isLoading = true
        state.log("⬇️ Đang tải '\(item.name)' (\(item.sizeString)) qua \(state.activeTunnel.rawValue)...")
        Task {
            do {
                let startTotal = CFAbsoluteTimeGetCurrent()
                let tempURL = FileManager.default.temporaryDirectory.appendingPathComponent(item.name)
                if FileManager.default.fileExists(atPath: tempURL.path) {
                    try FileManager.default.removeItem(at: tempURL)
                }
                FileManager.default.createFile(atPath: tempURL.path, contents: nil)
                let fileHandle = try FileHandle(forWritingTo: tempURL)
                defer { try? fileHandle.close() }
                
                var offset: UInt64 = 0
                let chunkSize: UInt32 = 32768
                
                while offset < item.size {
                    let remain = UInt32(min(UInt64(chunkSize), item.size - offset))
                    let data = try await state.dispatchRpc(opcode: .OP_READ, path: item.path, offset: offset, reqLen: remain, payloadData: nil)
                    if data.count > 27 {
                        let chunk = data.subdata(in: 27..<data.count)
                        try fileHandle.seekToEnd()
                        try fileHandle.write(contentsOf: chunk)
                        offset += UInt64(chunk.count)
                    } else {
                        break
                    }
                }
                
                let totalElapsed = CFAbsoluteTimeGetCurrent() - startTotal
                state.updateSpeed(bytes: Int(offset), durationSeconds: totalElapsed)
                
                await MainActor.run {
                    state.isLoading = false
                    state.log("✅ Tải xong '\(item.name)'! Tốc độ TB: \(state.transferSpeed)")
                    if openNativePreview {
                        DocumentPresenter.shared.presentNativeOrShare(for: tempURL)
                    } else {
                        DocumentPresenter.shared.presentShareSheet(for: tempURL)
                    }
                }
            } catch {
                await MainActor.run {
                    state.isLoading = false
                    state.log("❌ Lỗi tải file: \(error.localizedDescription)")
                }
            }
        }
    }
    
    func uploadFilesFromDevice(urls: [URL], clearPendingWhenDone: Bool = false) {
        guard !urls.isEmpty, state.currentPath != "/" else { return }
        state.isLoading = true
        
        Task {
            for url in urls {
                let fileName = url.lastPathComponent
                let destPath = state.currentPath.hasSuffix("/") ? "\(state.currentPath)\(fileName)" : "\(state.currentPath)/\(fileName)"
                state.log("⬆️ Đang tải '\(fileName)' lên \(state.displayPath(for: state.currentPath)) qua \(state.activeTunnel.rawValue)...")
                
                do {
                    let startTotal = CFAbsoluteTimeGetCurrent()
                    let hasSecurityScope = url.startAccessingSecurityScopedResource()
                    defer { if hasSecurityScope { url.stopAccessingSecurityScopedResource() } }
                    
                    let fileData = try Data(contentsOf: url)
                    let totalSize = fileData.count
                    
                    _ = try await state.dispatchRpc(opcode: .OP_TRUNCATE, path: destPath, offset: 0, reqLen: 0, payloadData: nil)
                    
                    var offset = 0
                    let chunkSize = 32768
                    while offset < totalSize {
                        let end = min(offset + chunkSize, totalSize)
                        let chunk = fileData.subdata(in: offset..<end)
                        _ = try await state.dispatchRpc(
                            opcode: .OP_WRITE,
                            path: destPath,
                            offset: UInt64(offset),
                            reqLen: UInt32(chunk.count),
                            payloadData: chunk
                        )
                        offset = end
                    }
                    
                    let totalElapsed = CFAbsoluteTimeGetCurrent() - startTotal
                    state.updateSpeed(bytes: totalSize, durationSeconds: totalElapsed)
                    state.log("✅ Đã tải lên '\(fileName)' (\(totalSize) Bytes) thành công!")
                } catch {
                    state.log("❌ Lỗi tải file '\(fileName)': \(error.localizedDescription)")
                }
            }
            
            if clearPendingWhenDone {
                state.clearPendingSharedFiles()
            }
            await MainActor.run { state.isLoading = false }
            await refreshList()
        }
    }
    
    func showFileInfo(_ item: ZhiFileItem) {
        let date = Date(timeIntervalSince1970: TimeInterval(item.modifiedTime))
        let formatter = DateFormatter()
        formatter.dateFormat = "dd/MM/yyyy HH:mm:ss"
        let virtualItemPath = state.displayPath(for: item.path)
        
        infoMessage = """
        Tên: \(item.name)
        Phân loại: \(item.isDirectory ? "Thư mục" : "Tập tin")
        Dung lượng: \(item.isDirectory ? "--" : "\(item.sizeString) (\(item.size) Bytes)")
        Cập nhật: \(formatter.string(from: date))
        Đường dẫn Mount: \(virtualItemPath)
        Giao thức: \(state.activeTunnel.rawValue)
        """
        showInfoAlert = true
    }
    
    func logout() {
        ZhiKcpEngine.stopCore()
        state.quicTunnel.disconnect()
        
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(state.baseIP), port: 5555)
        let conn = NWConnection(to: endpoint, using: .udp)
        conn.start(queue: .global())
        conn.send(content: "AUTH_REQ|LOGOUT".data(using: .utf8)!, completion: .contentProcessed { _ in
            conn.cancel()
        })
        
        state.isLoggedIn = false
        state.fileList = []
        state.currentPath = "/"
        state.clipboardItem = nil
        state.activeTunnel = .none
        state.transferSpeed = "0 KB/s"
        state.log("🛑 Đã đăng xuất và giải phóng toàn bộ tài nguyên KCP & QUIC.")
    }
    
    func parseList(data: Data, basePath: String) -> [ZhiFileItem] {
        var result: [ZhiFileItem] = []
        let headerSize = 27
        guard data.count > headerSize else { return result }
        
        var cur = headerSize
        while cur + 39 <= data.count {
            let nameLen = UInt16(data[cur]) | (UInt16(data[cur + 1]) << 8)
            let isDir = data[cur + 2] == 1
            var size: UInt64 = 0
            var mtime: UInt64 = 0
            withUnsafeMutableBytes(of: &size) { _ = data.copyBytes(to: $0, from: (cur + 3)..<(cur + 11)) }
            withUnsafeMutableBytes(of: &mtime) { _ = data.copyBytes(to: $0, from: (cur + 11)..<(cur + 19)) }
            
            cur += 39
            if cur + Int(nameLen) > data.count { break }
            
            if let name = String(data: data[cur..<(cur + Int(nameLen))], encoding: .utf8) {
                if name != "." && name != ".." {
                    let fullPath = basePath.hasSuffix("/") ? (basePath + name) : (basePath + "/" + name)
                    result.append(ZhiFileItem(name: name, isDirectory: isDir, size: size, modifiedTime: mtime, path: fullPath))
                }
            }
            cur += Int(nameLen)
        }
        return result
    }
}

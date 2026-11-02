import SwiftUI

@main
struct ZhiAuthApp: App {
    @StateObject private var appState = AppState()
    
    // Biến hứng URL file được ném từ app khác qua
    @State private var sharedFileURL: URL? = nil
    @State private var showUploadConfirm = false
    
    var body: some Scene {
        WindowGroup {
            Group {
                if appState.isLoggedIn {
                    FileExplorerView()
                        .environmentObject(appState)
                        // Bắt sự kiện iOS mở app và ném file vào
                        .onOpenURL { url in
                            // Copy file vào thư mục temp để tránh bị iOS thu hồi quyền truy cập
                            if let tempURL = copyToTemp(url: url) {
                                sharedFileURL = tempURL
                                showUploadConfirm = true
                            }
                        }
                        // Hiển thị hộp thoại hỏi người dùng có muốn upload không
                        .alert("Lưu file lên ZhiAuth", isPresented: $showUploadConfirm, presenting: sharedFileURL) { fileURL in
                            Button("Tải lên ngay", action: {
                                uploadSharedFile(url: fileURL)
                            })
                            Button("Hủy", role: .cancel) {
                                try? FileManager.default.removeItem(at: fileURL)
                            }
                        } message: { fileURL in
                            Text("Bạn muốn tải file '\(fileURL.lastPathComponent)' lên thư mục hiện tại: \(appState.displayPath(for: appState.currentPath)) không?")
                        }
                } else {
                    LoginView()
                        .environmentObject(appState)
                }
            }
            .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name("ZhiAuthLog"))) { note in
                if let msg = note.object as? String {
                    appState.log(msg)
                }
            }
        }
    }
    
    // Copy file vào thư mục Temp nội bộ của app để đọc an toàn
    private func copyToTemp(url: URL) -> URL? {
        let tempDir = FileManager.default.temporaryDirectory
        let destURL = tempDir.appendingPathComponent(url.lastPathComponent)
        do {
            if FileManager.default.fileExists(atPath: destURL.path) {
                try FileManager.default.removeItem(at: destURL)
            }
            try FileManager.default.copyItem(at: url, to: destURL)
            return destURL
        } catch {
            appState.log("❌ Lỗi tiếp nhận file chia sẻ: \(error.localizedDescription)")
            return nil
        }
    }
    
    // Bơm file lên Server qua luồng đang Active (KCP hoặc QUIC)
    private func uploadSharedFile(url: URL) {
        let fileName = url.lastPathComponent
        let destPath = appState.currentPath.hasSuffix("/") ? "\(appState.currentPath)\(fileName)" : "\(appState.currentPath)/\(fileName)"
        
        appState.isLoading = true
        appState.log("⬆️ Đang tải '\(fileName)' được Share từ ngoài vào Server qua \(appState.activeTunnel.rawValue)...")
        
        Task {
            do {
                let startTotal = CFAbsoluteTimeGetCurrent()
                let fileData = try Data(contentsOf: url)
                let totalSize = fileData.count
                
                _ = try await appState.dispatchRpc(opcode: .OP_TRUNCATE, path: destPath, offset: 0, reqLen: 0, payloadData: nil)
                
                var offset = 0
                let chunkSize = 32768
                while offset < totalSize {
                    let end = min(offset + chunkSize, totalSize)
                    let chunk = fileData.subdata(in: offset..<end)
                    _ = try await appState.dispatchRpc(
                        opcode: .OP_WRITE,
                        path: destPath,
                        offset: UInt64(offset),
                        reqLen: UInt32(chunk.count),
                        payloadData: chunk
                    )
                    offset = end
                }
                
                let totalElapsed = CFAbsoluteTimeGetCurrent() - startTotal
                appState.updateSpeed(bytes: totalSize, durationSeconds: totalElapsed)
                appState.log("✅ Đã tải file Share lên Server thành công!")
                
                // Dọn rác
                try? FileManager.default.removeItem(at: url)
                
                // Kích hoạt load lại thư mục
                await MainActor.run {
                    appState.isLoading = false
                    NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthRefresh"), object: nil)
                }
            } catch {
                await MainActor.run {
                    appState.isLoading = false
                    appState.log("❌ Lỗi tải file Share lên Server: \(error.localizedDescription)")
                }
            }
        }
    }
}

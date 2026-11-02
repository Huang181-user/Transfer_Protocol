import SwiftUI

@main
struct ZhiAuthApp: App {
    @StateObject private var appState = AppState()
    
    // Biến dùng để hứng file từ các App khác bắn qua Share Sheet
    @State private var sharedFileURL: URL? = nil
    @State private var showUploadConfirm = false
    
    var body: some Scene {
        WindowGroup {
            Group {
                if appState.isLoggedIn {
                    FileExplorerView()
                        .environmentObject(appState)
                        // Lắng nghe sự kiện iOS ném URL file vào App
                        .onOpenURL { url in
                            if let safeURL = copyFileToTempDir(url: url) {
                                self.sharedFileURL = safeURL
                                self.showUploadConfirm = true
                            } else {
                                appState.log("❌ Lỗi: Không thể sao chép file từ Share Sheet vào bộ nhớ đệm an toàn của ZhiAuth.")
                            }
                        }
                        // Bật popup hỏi xem ný muốn upload nó đi đâu
                        .alert("Lưu File lên ZhiAuth", isPresented: $showUploadConfirm, presenting: sharedFileURL) { fileURL in
                            Button("Lưu vào Thư mục hiện tại") {
                                uploadSharedFile(url: fileURL)
                            }
                            Button("Hủy", role: .cancel) {
                                try? FileManager.default.removeItem(at: fileURL)
                            }
                        } message: { fileURL in
                            Text("Bạn muốn đẩy tệp '\(fileURL.lastPathComponent)' lên thư mục [ \(appState.displayPath(for: appState.currentPath)) ] không?")
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
    
    // 🔥 Bảo vệ an toàn (Sandbox bypass): Copy URL được Share vào thư mục Tmp của App trước
    private func copyFileToTempDir(url: URL) -> URL? {
        // Có những trường hợp iOS đòi cấp quyền bảo mật
        let hasSecurityScope = url.startAccessingSecurityScopedResource()
        defer { if hasSecurityScope { url.stopAccessingSecurityScopedResource() } }
        
        let tempDir = FileManager.default.temporaryDirectory
        let destURL = tempDir.appendingPathComponent(url.lastPathComponent)
        
        do {
            if FileManager.default.fileExists(atPath: destURL.path) {
                try FileManager.default.removeItem(at: destURL)
            }
            try FileManager.default.copyItem(at: url, to: destURL)
            return destURL
        } catch {
            return nil
        }
    }
    
    // Gọi hàm upload từ xa trong FileExplorerView thông qua AppState (tránh nhúng view loạn xạ)
    private func uploadSharedFile(url: URL) {
        let fileName = url.lastPathComponent
        let destPath = appState.currentPath.hasSuffix("/") ? "\(appState.currentPath)\(fileName)" : "\(appState.currentPath)/\(fileName)"
        
        appState.isLoading = true
        appState.log("⬆️ Đang tải tệp '\(fileName)' từ Share Sheet lên Server qua luồng \(appState.activeTunnel.rawValue)...")
        
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
                appState.log("✅ Đã nhận và tải file Share lên Server thành công!")
                
                try? FileManager.default.removeItem(at: url) // Dọn rác file tạm
                await MainActor.run { appState.isLoading = false }
                
                // Cập nhật lại UI thông qua Notification
                NotificationCenter.default.post(name: NSNotification.Name("ZhiAuthRefreshList"), object: nil)
            } catch {
                await MainActor.run {
                    appState.isLoading = false
                    appState.log("❌ Lỗi Upload file Share: \(error.localizedDescription)")
                }
            }
        }
    }
}

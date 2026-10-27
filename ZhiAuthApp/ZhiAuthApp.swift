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
    
    var sharedRoot: String = ""
    var baseIP: String = ""
    
    func log(_ msg: String) {
        DispatchQueue.main.async {
            let timestamp = Date().formatted(date: .omitted, time: .standard)
            self.logs += "[\(timestamp)] \(msg)\n"
            if self.logs.count > 3000 { self.logs = String(self.logs.suffix(2000)) }
        }
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
            
            if state.isLoading {
                ProgressView("Đang khởi động KCP Engine...").padding()
            }
            
            Button(action: performLogin) {
                Text("KẾT NỐI SERVER")
                    .font(.headline).foregroundColor(.white).padding()
                    .frame(maxWidth: .infinity).background(state.isLoading ? Color.gray : Color.blue).cornerRadius(10)
            }.disabled(state.isLoading).padding(.horizontal, 30)
            
            ScrollView {
                Text(state.logs).font(.system(size: 10, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading).padding()
            }.background(Color.black.opacity(0.8)).foregroundColor(.green).cornerRadius(10).padding()
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
                    
                    // Thử check rễ
                    _ = try await ZhiKcpEngine.sendRpcVfs(opcode: .OP_STAT, path: state.sharedRoot, offset: 0, reqLen: 0, payloadData: nil)
                    
                    await MainActor.run {
                        state.currentPath = state.sharedRoot
                        state.isLoggedIn = true
                        state.isLoading = false
                    }
                } else {
                    state.log("❌ Khởi động KCP Engine thất bại.")
                    state.isLoading = false
                }
            } catch {
                state.log("❌ Lỗi: \(error.localizedDescription)")
                state.isLoading = false
            }
        }
    }
}

struct FileExplorerView: View {
    @EnvironmentObject var state: AppState
    @State private var isRefreshing = false
    
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
                        Image(systemName: item.isDirectory ? "folder.fill" : "doc.text.fill")
                            .foregroundColor(item.isDirectory ? .blue : .gray)
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
                            // TODO: Mở file
                        }
                    }
                }
                .refreshable { await refreshList() }
                .overlay(Group {
                    if state.fileList.isEmpty && !state.isLoading { Text("Thư mục rỗng").foregroundColor(.gray) }
                })
            }
            .navigationTitle("ZhiAuth Explorer")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    if state.currentPath != state.sharedRoot {
                        Button(action: goUp) {
                            HStack { Image(systemName: "chevron.backward"); Text("Quay lại") }
                        }
                    }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: logout) { Image(systemName: "power").foregroundColor(.red) }
                }
            }
            .onAppear { loadDirectory(path: state.currentPath) }
        }
    }
    
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
                    state.fileList = parsedList.sorted { $0.isDirectory && !$1.isDirectory }
                    state.isLoading = false
                }
            } catch {
                await MainActor.run { state.log("❌ Lỗi load thư mục: \(error.localizedDescription)"); state.isLoading = false }
            }
        }
    }
    
    func refreshList() async {
        loadDirectory(path: state.currentPath)
    }
    
    func logout() {
        ZhiKcpEngine.stopCore()
        // Đóng cổng UFW ảo nếu cần
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(state.baseIP), port: 5555)
        let conn = NWConnection(to: endpoint, using: .udp)
        conn.start(queue: .global())
        conn.send(content: "AUTH_REQ|LOGOUT".data(using: .utf8)!, completion: .contentProcessed { _ in conn.cancel() })
        
        state.isLoggedIn = false
        state.fileList = []
        state.currentPath = "/"
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

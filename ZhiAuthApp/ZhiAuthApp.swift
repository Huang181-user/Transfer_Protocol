import SwiftUI

@main
struct ZhiAuthApp: App {
    @StateObject private var appState = AppState()
    
    var body: some Scene {
        WindowGroup {
            Group {
                if appState.isLoggedIn {
                    FileExplorerView()
                        .environmentObject(appState)
                } else {
                    LoginView()
                        .environmentObject(appState)
                }
            }
            .onOpenURL { url in
                appState.enqueueSharedFile(url: url)
            }
            .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name("ZhiAuthLog"))) { note in
                if let msg = note.object as? String {
                    appState.log(msg)
                }
            }
        }
    }
}

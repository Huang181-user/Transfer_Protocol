import SwiftUI
import UIKit
import UniformTypeIdentifiers

class DocumentPresenter: NSObject, UIDocumentInteractionControllerDelegate {
    static let shared = DocumentPresenter()
    private var documentController: UIDocumentInteractionController?

    private func topViewController() -> UIViewController? {
        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let window = scene.windows.first(where: { $0.isKeyWindow }) ?? scene.windows.first,
              var topVC = window.rootViewController else { return nil }
        while let presented = topVC.presentedViewController {
            topVC = presented
        }
        return topVC
    }

    func presentNativeOrShare(for url: URL) {
        DispatchQueue.main.async {
            let controller = UIDocumentInteractionController(url: url)
            controller.delegate = self
            self.documentController = controller
            
            let canPreview = controller.presentPreview(animated: true)
            if !canPreview {
                self.presentShareSheet(for: url)
            }
        }
    }
    
    func presentShareSheet(for url: URL) {
        DispatchQueue.main.async {
            guard let topVC = self.topViewController() else { return }
            let activityVC = UIActivityViewController(activityItems: [url], applicationActivities: nil)
            if UIDevice.current.userInterfaceIdiom == .pad {
                activityVC.popoverPresentationController?.sourceView = topVC.view
                activityVC.popoverPresentationController?.sourceRect = CGRect(x: topVC.view.bounds.midX, y: topVC.view.bounds.midY, width: 0, height: 0)
                activityVC.popoverPresentationController?.permittedArrowDirections = []
            }
            topVC.present(activityVC, animated: true)
        }
    }
    
    func documentInteractionControllerViewControllerForPreview(_ controller: UIDocumentInteractionController) -> UIViewController {
        return topViewController() ?? UIViewController()
    }
}

struct LocalFilePicker: UIViewControllerRepresentable {
    var onPick: ([URL]) -> Void
    
    func makeCoordinator() -> Coordinator {
        Coordinator(onPick: onPick)
    }
    
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.item], asCopy: true)
        picker.delegate = context.coordinator
        picker.allowsMultipleSelection = true
        return picker
    }
    
    func updateUIViewController(_ uiViewController: UIDocumentPickerViewController, context: Context) {}
    
    class Coordinator: NSObject, UIDocumentPickerDelegate {
        let onPick: ([URL]) -> Void
        init(onPick: @escaping ([URL]) -> Void) { self.onPick = onPick }
        
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            guard !urls.isEmpty else { return }
            onPick(urls)
        }
    }
}

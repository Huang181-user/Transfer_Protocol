import SwiftUI

struct FileRowView: View {
    let item: ZhiFileItem
    let isRootTunnelMenu: Bool
    let onTap: () -> Void
    let onInfo: () -> Void
    let onCopy: () -> Void
    let onCut: () -> Void
    let onRename: () -> Void
    let onSaveToDevice: () -> Void
    let onDelete: () -> Void
    
    private var iconName: String {
        if isRootTunnelMenu {
            return item.name == "ZhiAuth_KCP" ? "bolt.Custom.network" : "shield.lefthalf.filled"
        }
        return item.isDirectory ? "folder.fill" : "doc.text.fill"
    }
    
    private var systemIconName: String {
        if isRootTunnelMenu {
            return item.name == "ZhiAuth_KCP" ? "network" : "bolt.horizontal.circle.fill"
        }
        return item.isDirectory ? "folder.fill" : "doc.text.fill"
    }
    
    private var iconColor: Color {
        if isRootTunnelMenu {
            return item.name == "ZhiAuth_KCP" ? .blue : .orange
        }
        return item.isDirectory ? .blue : .teal
    }
    
    private var subtitleText: String {
        if isRootTunnelMenu {
            return item.name == "ZhiAuth_KCP"
                ? "Luồng chính: C++ KCP UDP Siêu tốc (Tự động dự phòng QUIC)"
                : "Luồng dự phòng: MsQUIC TLS 1.3 (Xuyên tường lửa)"
        }
        return item.sizeString
    }
    
    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 14) {
                Image(systemName: systemIconName)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: 28, height: 28)
                    .foregroundColor(iconColor)
                
                VStack(alignment: .leading, spacing: 4) {
                    Text(item.name)
                        .font(.body.weight(.medium))
                        .foregroundColor(.primary)
                        .lineLimit(1)
                    Text(subtitleText)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
                
                Spacer()
                
                Image(systemName: "chevron.right")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(FileItemButtonStyle())
        .contextMenu {
            if !isRootTunnelMenu {
                Button(action: onInfo) {
                    Label("Thông tin chi tiết", systemImage: "info.circle")
                }
                if !item.isDirectory {
                    Button(action: onSaveToDevice) {
                        Label("Lưu về iPhone / Chia sẻ", systemImage: "square.and.arrow.down")
                    }
                }
                Button(action: onCopy) {
                    Label("Sao chép (Copy)", systemImage: "doc.on.doc")
                }
                Button(action: onCut) {
                    Label("Di chuyển (Cut)", systemImage: "scissors")
                }
                Button(action: onRename) {
                    Label("Đổi tên (Rename)", systemImage: "pencil")
                }
                Button(role: .destructive, action: onDelete) {
                    Label("Xóa vĩnh viễn", systemImage: "trash")
                }
            }
        }
    }
}

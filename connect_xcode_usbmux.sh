#!/bin/bash

# In log Realtime kèm timestamp millisecond
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S.%3N')] $1"
}

log "=========================================================="
log "🛑 [STEP 1/3] TẮT USBMUXD NỘI BỘ MẶC ĐỊNH CỦA MACOS"
log "=========================================================="
sudo launchctl unload -w /System/Library/LaunchDaemons/com.apple.usbmuxd.plist 2>/dev/null
sudo killall usbmuxd 2>/dev/null
sleep 1

log "=========================================================="
log "🧹 [STEP 2/3] DỌN FILE SOCKET CŨ TẠI /var/run/usbmuxd"
log "=========================================================="
sudo rm -f /var/run/usbmuxd

log "=========================================================="
log "🌐 [STEP 3/3] BRIDGE UNIX SOCKET SANG LINUX HOST (192.168.1.25:22222)"
log "=========================================================="
log "Đang mở cầu nối cho Xcode... (Giữ cửa sổ này chạy ngầm hoặc bấm Ctrl+C để dừng)"

# Nối /var/run/usbmuxd của macOS sang Linux Host qua socat
sudo socat -v -d -d UNIX-LISTEN:/var/run/usbmuxd,fork,reuseaddr,mode=777 TCP:192.168.1.25:22222

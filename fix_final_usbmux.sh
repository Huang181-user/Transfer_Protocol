#!/bin/bash

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S.%3N')] $1"
}

log "=========================================================="
log "🚫 [STEP 1/4] BLACKLIST VĨNH VIỄN DRIVER CHÂU CHẤU IPHETH & FASTCHARGE"
log "=========================================================="
# Gỡ driver ngầm đang chiếm cổng
sudo modprobe -r ipheth apple-mfi-fastcharge 2>/dev/null || true

# Khóa vĩnh viễn không cho Linux Kernel nạp lại 2 driver này
cat << 'MOD_EOF' | sudo tee /etc/modprobe.d/blacklist-iphone-conflicts.conf
blacklist ipheth
blacklist apple-mfi-fastcharge
MOD_EOF

log "=========================================================="
log "⚙️ [STEP 2/4] FIX UDEV RULE CẤP QUYỀN VĨNH VIỄN CHO APPLE USB"
log "=========================================================="
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="05ac", MODE="0666"' | sudo tee /etc/udev/rules.d/99-iphone-autochmod.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

log "=========================================================="
log "🔑 [STEP 3/4] CẤU HÌNH SYSTEMD CẤM USBMUXD DROPPPRIVILEGES"
log "=========================================================="
# Ép usbmuxd chạy bằng root VÀ truyền cờ --user root để cấm nó tự hạ quyền
sudo mkdir -p /etc/systemd/system/usbmuxd.service.d/
cat << 'SERVICE_EOF' | sudo tee /etc/systemd/system/usbmuxd.service.d/override.conf
[Service]
User=root
Group=root
ExecStart=
ExecStart=/usr/sbin/usbmuxd --user root --foreground
SERVICE_EOF

sudo systemctl daemon-reload
sudo systemctl restart usbmuxd
sleep 1.5

log "=========================================================="
log "📱 [STEP 4/4] PHÁT LỆNH PAIR CHỐT SỔ"
log "=========================================================="
UDID=$(idevice_id -l)

if [ -n "$UDID" ]; then
    log "🎉 THÀNH CÔNG RỰC RỠ! ĐÃ ĐỌC ĐƯỢC UDID: $UDID"
    log "👉 Đang phát lệnh Pair..."
    idevicepair pair
else
    log "⚠️ Chưa thấy UDID ngay lập tức. Ný hãy RÚT CÁP IPHONE RA CẮM LẠI VÀ MỞ SÁNG MÀN HÌNH nhé!"
fi

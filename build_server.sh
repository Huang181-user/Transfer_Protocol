#!/bin/bash
set -e

echo -e "\n🛑 [1/5] Đang rút ống thở Service cũ và dọn rác KCP Worker..."
sudo systemctl stop zhiauth || true
sudo pkill -9 -f zhiauth_kcp_worker || true

echo -e "\n🧹 [2/5] Đang dọn dẹp và đúc lại lõi C++ (Server)..."
cd ~/zhiauth
make clean 2>/dev/null || true
cmake .
make -j$(nproc)

echo -e "\n🐹 [3/5] Đang đúc lại tiền đồn Go Gateway..."
cd ~/zhiauth/go_gateway
go build -o zhiauth_gateway .

echo -e "\n⚙️ [4/5] Đang sao chép Gateway & KCP Worker vào hệ thống..."
# COPY LÕI C++
sudo cp ../zhiauth_kcp_worker /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_kcp_worker
# COPY GO GATEWAY (ĐÃ THÊM VÀO ĐÂY)
sudo cp zhiauth_gateway /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_gateway

echo -e "\n🚀 [5/5] Kích nổ lại Service..."
sudo systemctl start zhiauth
sudo systemctl status zhiauth --no-pager

echo -e "\n✅ XONG! Server đã hoạt động mượt mà trở lại."
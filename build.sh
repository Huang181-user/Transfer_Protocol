#!/bin/bash
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log() { echo -e "[$GREEN$(date +'%Y-%m-%d %H:%M:%S.%3N')$NC] $1"; }

log "${YELLOW}=======================================================${NC}"
log "${YELLOW}🚀 BẮT ĐẦU BIÊN DỊCH PURE C++ SERVER & RESTART SERVICE${NC}"
log "${YELLOW}=======================================================${NC}"

mkdir -p build && cd build

log "${YELLOW}[CMAKE] Đang tạo cấu hình Makefile...${NC}"
cmake ..
if [ $? -ne 0 ]; then log "${RED}❌ Lỗi cấu hình CMake!${NC}"; exit 1; fi

log "${YELLOW}[MAKE] Đang biên dịch ép xung với $(nproc) luồng...${NC}"
make -j$(nproc)
if [ $? -ne 0 ]; then log "${RED}❌ Biên dịch thất bại!${NC}"; exit 1; fi

cd ..
log "${GREEN}=======================================================${NC}"
log "${GREEN}✅ BIÊN DỊCH HOÀN TẤT. TIẾN HÀNH DEPLOY VÀO HỆ THỐNG...${NC}"
log "${GREEN}=======================================================${NC}"

log "${YELLOW}[SERVICE] Đang dừng tiến trình zhiauth.service...${NC}"
sudo systemctl stop zhiauth

log "${YELLOW}[DEPLOY] Sao chép file thực thi vào /usr/local/bin/...${NC}"
sudo cp build/zhiauth_gateway /usr/local/bin/
sudo cp build/zhiauth_kcp_worker /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_gateway
sudo chmod +x /usr/local/bin/zhiauth_kcp_worker

log "${YELLOW}[SERVICE] Đang khởi động lại zhiauth.service...${NC}"
sudo systemctl start zhiauth

log "${GREEN}✅ QUÁ TRÌNH TRIỂN KHAI THÀNH CÔNG!${NC}"
sudo systemctl status zhiauth --no-pager | head -n 10

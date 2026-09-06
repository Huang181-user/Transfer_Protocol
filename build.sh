#!/bin/bash
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log() { echo -e "[$GREEN$(date +'%Y-%m-%d %H:%M:%S.%3N')$NC] $1"; }

log "${YELLOW}=======================================================${NC}"
log "${YELLOW}🚀 BẮT ĐẦU BIÊN DỊCH PURE C++ CLIENT (LINUX FUSE API)${NC}"
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
log "${GREEN}✅ PURE C++ CLIENT LINUX ĐÃ SẴN SÀNG TẠI: build/zhiauth_client_app${NC}"
log "${GREEN}=======================================================${NC}"

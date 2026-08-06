#!/bin/bash
set -e

echo -e "\n🧹 [1/3] Đang dọn dẹp và đúc lại lõi C++ (Client trong thư mục build)..."
cd ~/zhiauth_client
# Tiêu diệt tàn dư ở thư mục gốc
rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile libzhiauth_client_core.a build/

mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo -e "\n🐹 [2/3] Đang đúc lại Go Client..."
cd ~/zhiauth_client/go_client
go build -o zhiauth_client .

echo -e "\n🚀 [3/3] Hoàn tất! Kích nổ Client..."
sudo ./zhiauth_client
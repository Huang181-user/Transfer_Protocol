#!/bin/bash
set -e

export ANDROID_NDK_HOME=/home/huang/Android/Sdk/ndk/28.2.13676358

echo -e "\n📱 [1/2] Đang gọi GoMobile đúc thư viện Android AAR..."
cd ~/Documents/transfer_server/Go_mobile
~/go/bin/gomobile bind -target=android -androidapi 21 -o quicdroid.aar .

echo -e "\n📦 [2/2] Đang chép quicdroid.aar vào thư mục app/libs..."
cp quicdroid.aar ../app/libs/quicdroid.aar

echo -e "\n✅ XONG! Mở Android Studio lên và quẩy thôi ný!"

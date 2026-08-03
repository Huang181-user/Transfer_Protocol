<a id="top"></a>

<div align="center">
# 📱 Transfer Protocol / ZhiAuth - Client Android


 
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-emerald.svg)](https://en.cppreference.com/w/cpp/20)
[![Go Version](https://img.shields.io/badge/Go-1.25%2B-00ADD8.svg)](https://golang.org/)
[![FUSE Engine](https://img.shields.io/badge/Go--FUSE-v2.5.1-orange.svg)](https://github.com/hanwen/go-fuse)
</div>

[ 🇻🇳 **Tiếng Việt**](#vietnamese) | [ 🇬🇧 **English**](#english) | [ 🇨🇳 **中文**](#chinese)

---

<a id="vietnamese"></a>
## 🇻🇳 Tiếng Việt

### 📖 Giới thiệu Module
Đây là nhánh mã nguồn dành riêng cho **Client Android** thuộc hệ sinh thái **Transfer Protocol (ZhiAuth)**. Ứng dụng được xây dựng theo kiến trúc Native lai (Hybrid Native), kết hợp giữa giao diện Kotlin Jetpack Compose hiện đại với C++ NDK Core Engine và Go Mobile Runtime để đạt hiệu năng mã hóa và truyền tải dữ liệu cực cao trên thiết bị di động.

---

### 🏗️ Cấu trúc Công nghệ (Tech Stack)

* **UI Layer:** Kotlin, Jetpack Compose, Material Design 3.
* **Core Native Engine (C++ NDK):**
  * `zhiauth_jni.cpp`: Cầu nối JNI giữa Kotlin và Native Core.
  * `crypto_box`: Mã hóa / Giải mã bảo mật cao bằng **Libsodium** (Ed25519, ChaCha20-Poly1305).
  * `ikcp`: Giao thức KCP UDP tối ưu độ trễ thấp.
* **Go Mobile Bridge:** `quicdroid.aar` (QUIC Client Engine) xử lý truyền tải stream đa luồng qua gRPC/QUIC.
* **Storage Access Framework (SAF):** `HuangDocumentsProvider` hỗ trợ gắn VFS vào hệ thống Android, duyệt tệp trực tiếp trong ứng dụng Files gốc.
* **Debugging & Logging:** `RealtimeLogger` ghi log chi tiết kèm mốc thời gian thực hỗ trợ debug.



### 📂 Cấu trúc Thư mục Chính

```text
app/
 ├── src/main/
 │    ├── cpp/                  # C++ Native Code (Libsodium, KCP, JNI)
 │    │    ├── zhiauth_jni.cpp
 │    │    ├── crypto_box.cpp
 │    │    └── ikcp.c
 │    ├── java/com/example/transfer_server/
 │    │    ├── HuangDocumentsProvider.kt   # Provider SAF truy xuất tệp từ xa
 │    │    ├── KcpClientEngine.kt          # Động cơ KCP Client
 │    │    ├── QuicClientEngine.kt         # Động cơ QUIC Client
 │    │    ├── ZhiAuthService.kt           # Background Service điều phối
 │    │    └── RealtimeLogger.kt           # Log Realtime debug
 │    └── res/raw/                         # Chứng chỉ SSL/TLS (.crt)
 └── libs/
      └── quicdroid.aar                    # Module Go Mobile QUIC
Go_mobile/                                 # Mã nguồn Go build ra quicdroid.aar

```

---

### ✨ Tính năng nổi bật trên Android

1. **Mã hóa đầu-cuối (E2EE):** Tích hợp thư viện C++ Libsodium lõi, mã hóa gói tin trực tiếp ở tầng Native trước khi gửi qua mạng.
2. **Dual Transport Engine:** Cho phép linh hoạt chuyển đổi hoặc kết hợp giữa KCP (UDP độ trễ thấp) và QUIC (Go Mobile).
3. **Android Storage Access Framework (SAF):** Duyệt, đọc/ghi tệp từ xa từ Ubuntu Server trực tiếp thông qua ứng dụng "Files" (Tệp) gốc của Android mà không cần tải toàn bộ tệp về máy.
4. **Realtime Debug Logging:** Hệ thống Log hiển thị chi tiết thời gian thực giúp theo dõi luồng bắt tay (Handshake) và truyền nhận dữ liệu.

---

### 🛠️ Hướng dẫn Biên dịch & Chạy

#### Yêu cầu môi trường:

* **Android Studio:** Ladybug (2024.2.1) trở lên hoặc bản mới nhất.
* **JDK:** Java 21.
* **Android NDK:** Bản NDK tương thích (đã cấu hình trong CMake/Gradle).
* **Go:** >= 1.22 (Chỉ cần nếu bạn muốn build lại `quicdroid.aar` từ thư mục `Go_mobile`).

#### Các bước thực hiện:

```bash
# 1. Checkout sang branch Client_Android
git checkout Client_Android

# 2. Biên dịch gói Debug APK
./gradlew assembleDebug

# 3. Hoặc biên dịch Release APK
./gradlew assembleRelease

```

---


<a id="english"></a>
## 🇬🇧 English

### 📖 Module Overview

This branch hosts the **Android Client** implementation for the **Transfer Protocol (ZhiAuth)** ecosystem. The app utilizes a Hybrid Native architecture—combining Jetpack Compose for modern UI with a high-performance C++ NDK Core Engine and Go Mobile Runtime to achieve ultra-fast encryption and low-latency transport on mobile devices.

---

### 🏗️ Tech Stack

* **UI Layer:** Kotlin, Jetpack Compose, Material Design 3.
* **Core Native Engine (C++ NDK):**
* `zhiauth_jni.cpp`: JNI Bridge interconnecting Kotlin and Native code.
* `crypto_box`: Libsodium C++ cryptographic operations (Ed25519, ChaCha20-Poly1305).
* `ikcp`: Low-latency KCP UDP protocol engine.


* **Go Mobile Bridge:** `quicdroid.aar` (Go-powered QUIC Client Engine) handling multiplexed streaming transport.
* **Storage Access Framework (SAF):** `HuangDocumentsProvider` exposing remote VFS directly into the native Android Files app.
* **Debugging & Logging:** Integrated `RealtimeLogger` providing realtime timestamps for painless debugging.

---

### 📂 Repository Layout

```text
app/
 ├── src/main/
 │    ├── cpp/                  # C++ Native Sources (Libsodium, KCP, JNI)
 │    │    ├── zhiauth_jni.cpp
 │    │    ├── crypto_box.cpp
 │    │    └── ikcp.c
 │    ├── java/com/example/transfer_server/
 │    │    ├── HuangDocumentsProvider.kt   # SAF Content Provider
 │    │    ├── KcpClientEngine.kt          # KCP Transport Driver
 │    │    ├── QuicClientEngine.kt         # QUIC Transport Driver
 │    │    ├── ZhiAuthService.kt           # Background Service
 │    │    └── RealtimeLogger.kt           # Realtime Debug Logger
 │    └── res/raw/                         # TLS/SSL Certificates (.crt)
 └── libs/
      └── quicdroid.aar                    # Go Mobile compiled AAR
Go_mobile/                                 # Go Mobile source project

```

---

### ✨ Key Android Features

1. **End-to-End Encryption (E2EE):** Native Libsodium cryptographic integration encrypting payloads at the C++ layer before packet transmission.
2. **Dual Transport Engine:** Seamless switching between KCP (low-latency UDP) and QUIC (Go Mobile engine).
3. **Storage Access Framework (SAF):** Native `HuangDocumentsProvider` allowing remote file browsing, streaming, and editing directly inside Android's default Files application.
4. **Realtime Logger:** Comprehensive log output with precise timestamps to simplify connection handshake and state debugging.

---

### 🛠️ Build Instructions

#### Prerequisites:

* **Android Studio:** Ladybug or newer.
* **JDK:** Java 21.
* **Android NDK:** Installed via SDK Manager.
* **Go:** >= 1.22 (Required only when rebuilding `quicdroid.aar` from `Go_mobile`).

#### Commands:

```bash
# 1. Checkout to the Client_Android branch
git checkout Client_Android

# 2. Build Debug APK
./gradlew assembleDebug

# 3. Build Release APK
./gradlew assembleRelease

```

---


<a id="chinese"></a>
## 🇨🇳 中文

### 📖 模块简介

本分支为 **Transfer Protocol (ZhiAuth)** 生态系统的 **Android 客户端** 源码。应用采用混合原生 (Hybrid Native) 架构，将现代化的 Kotlin Jetpack Compose 界面与高性能 C++ NDK 核心引擎以及 Go Mobile 运行时相结合，在移动设备上实现极致的加解密与低延迟数据传输性能。

---

### 🏗️ 技术栈

* **UI 层：** Kotlin, Jetpack Compose, Material Design 3。
* **C++ NDK 核心引擎：**
* `zhiauth_jni.cpp`: JNI 桥接层，连接 Kotlin 与 C++ 核心。
* `crypto_box`: 基于 Libsodium 的 C++ 加密模块 (Ed25519, ChaCha20-Poly1305)。
* `ikcp`: KCP UDP 低延迟传输协议引擎。


* **Go Mobile 桥接：** `quicdroid.aar`（基于 Go 的 QUIC 客户端引擎），处理多路复用流传输。
* **Storage Access Framework (SAF)：** `HuangDocumentsProvider` 实现，将远程虚拟文件系统无缝挂载至 Android 原生“文件”应用。
* **调试与日志：** 集成 `RealtimeLogger` 实时日志系统，附带精准时间戳，方便排查与调试。

---

### 📂 目录结构

```text
app/
 ├── src/main/
 │    ├── cpp/                  # C++ 原生源码 (Libsodium, KCP, JNI)
 │    │    ├── zhiauth_jni.cpp
 │    │    ├── crypto_box.cpp
 │    │    └── ikcp.c
 │    ├── java/com/example/transfer_server/
 │    │    ├── HuangDocumentsProvider.kt   # SAF 内容提供者
 │    │    ├── KcpClientEngine.kt          # KCP 传输引擎
 │    │    ├── QuicClientEngine.kt         # QUIC 传输引擎
 │    │    ├── ZhiAuthService.kt           # 后台调度服务
 │    │    └── RealtimeLogger.kt           # 实时调试日志
 │    └── res/raw/                         # SSL/TLS 证书 (.crt)
 └── libs/
      └── quicdroid.aar                    # Go Mobile 编译产物
Go_mobile/                                 # Go Mobile 源码工程

```

---

### ✨ Android 端核心功能

1. **端到端加密 (E2EE)：** 原生 Libsodium 加密库集成，数据包在 C++ 原生层完成加密后再进行网络发送。
2. **双传输引擎：** 支持在 KCP（低延迟 UDP）与 QUIC (Go Mobile) 传输协议之间灵活切换。
3. **存储接入框架 (SAF)：** 实现原生 `HuangDocumentsProvider`，无需下载整个文件即可在 Android 系统自带的“文件”管理器中直接浏览、读写远程文件。
4. **实时日志系统：** 带有实时时间戳的日志输出，极大简化握手协议与数据传输的调试流程。

---

### 🛠️ 编译与构建

#### 环境要求：

* **Android Studio：** Ladybug (2024.2.1) 或更高版本。
* **JDK：** Java 21。
* **Android NDK：** 通过 SDK Manager 完成安装。
* **Go：** >= 1.22（仅在需要从 `Go_mobile` 重新构建 `quicdroid.aar` 时需要）。

#### 构建命令：

```bash
# 1. 切换至 Client_Android 分支
git checkout Client_Android

# 2. 编译 Debug APK
./gradlew assembleDebug

# 3. 编译 Release APK
./gradlew assembleRelease

```

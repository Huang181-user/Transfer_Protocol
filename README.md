<div align="center">

# 🚀 Transfer Protocol / ZhiAuth Ecosystem

### **High-Performance, Zero-Latency C++/Go VFS Client for MAIN SERVER**

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-emerald.svg)](https://en.cppreference.com/w/cpp/20)
[![Go Version](https://img.shields.io/badge/Go-1.25%2B-00ADD8.svg)](https://golang.org/)
[![FUSE Engine](https://img.shields.io/badge/Go--FUSE-v2.5.1-orange.svg)](https://github.com/hanwen/go-fuse)
</div>

[ 🇻🇳 **Tiếng Việt**](#vietnamese) | [ 🇬🇧 **English**](#english) | [ 🇨🇳 **中文**](#chinese)

---
<a id="vietnamese"></a>
## 🇻🇳 Tiếng Việt

### 📖 Giới thiệu
**Transfer_Protocol (ZhiAuth)** là hệ sinh thái truyền tải dữ liệu và xác thực hiệu năng cao, kết hợp giữa **C++ (Core Engine)** và **Go (Networking & Bridge)**. Hệ thống được thiết kế theo kiến trúc Client-Server bảo mật cao, hỗ trợ mã hóa đầu-cuối, tối ưu giao thức truyền tải UDP qua KCP/QUIC, vượt NAT và hỗ trợ gắn hệ thống tệp ảo (VFS / SAF) trên Windows, Linux và Android.

---

### 🌿 Cấu trúc nhánh Git (Branches)

| Nhánh (Branch) | Thành phần | Mô tả chi tiết |
| :--- | :--- | :--- |
| **`main_server`** | **Ubuntu Server** | Máy chủ trung tâm (`zhiauth_gateway`), xử lý xác thực phiên, điều phối kết nối, KCP/QUIC Server Engine, backend VFS/NFS và tối ưu hóa cửa sổ truyền tải KCP. |
| **`Client_Windows`** | **Windows Client** | Client chạy trên Windows, tích hợp C++ Core, Go Bridge (CGO), gắn ổ đĩa ảo qua WinFSP/WebDAV, tự động đo MTU (`huang_mtu_radar`) và lấy định danh phần cứng. |
| **`Client_Linux`** | **Linux Client** | Client chạy trên Linux, hỗ trợ FUSE/VFS, giao thức KCP/QUIC Tunneling, tương thích daemon/systemd. |
| **`Client_Android`** | **Android Client** | Client di động phát triển bằng Jetpack Compose, C++ NDK (`zhiauth_jni`, Libsodium, KCP), Go Mobile (`quicdroid.aar`), tích hợp Storage Access Framework (`HuangDocumentsProvider`). |

---

### ✨ Tính năng nổi bật

#### 1. Server Core (`main_server`)
* **Gateway & Authentication (`zhiauth_gateway`)**: Quản lý phiên đăng nhập, xác thực client, phân quyền và điều phối kết nối.
* **KCP & QUIC Server Engine**: Tối ưu hóa cửa sổ truyền dữ liệu UDP (`IKCP_WND_RCV`), giảm thiểu đệm và chống nghẽn đường truyền độ trễ cao.
* **VFS Backend**: Quản lý và xử lý yêu cầu truy xuất tệp từ các Client từ xa.

#### 2. Windows Client (`Client_Windows`)
* **C++ Core & Go CGO Bridge**: Kết nối trực tiếp hiệu năng cao giữa C++ và Go.
* **Virtual Drive Mounting**: Hỗ trợ gắn ổ đĩa ảo trực tiếp vào Windows Explorer qua **WinFSP** hoặc **WebDAV**.
* **Radar MTU & Hardware Identity**: Tự động đo đạc kích thước MTU mạng (`huang_mtu_radar`) và tạo dấu vân tay phần cứng (`hw_fingerprint`).

#### 3. Linux Client (`Client_Linux`)
* **FUSE VFS Integration**: Gắn kết nối tệp ảo trực tiếp vào file system của Linux qua FUSE.
* **QUIC & KCP Tunneling**: Thiết lập đường truyền bảo mật và độ trễ thấp tới Ubuntu Server.

#### 4. Android Client (`Client_Android`)
* **Kotlin Compose & Native NDK**: Giao diện Compose hiện đại kết hợp C++ NDK Engine (`zhiauth_jni`, Libsodium, KCP) cho hiệu năng xử lý mã hóa và truyền tải tối đa.
* **Go Mobile Integration**: Đóng gói thư viện Go Mobile (`quicdroid.aar`) giúp xử lý kết nối QUIC mượt mà trên môi trường di động.
* **Storage Access Framework (SAF)**: Tích hợp `HuangDocumentsProvider` giúp duyệt, đọc/ghi và quản lý tệp từ xa trực tiếp trong ứng dụng Quản lý tệp (Files) gốc của Android.

---

### 🛠️ Hướng dẫn Cài đặt & Triển khai Server

#### 1. Biên dịch Server (Ubuntu Linux)
*Yêu cầu:* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`, `cmake`.

```bash
# Checkout sang branch server
git checkout main_server

# Tạo thư mục build và biên dịch C++ Core
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Di chuyển các file nhị phân vào thư mục hệ thống
sudo cp zhiauth_server_app /usr/local/bin/
sudo cp zhiauth_kcp_worker /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_server_app
sudo chmod +x /usr/local/bin/zhiauth_kcp_worker
cd ..

#### 2. Cấu hình Tường lửa (UFW)

ZhiAuth sử dụng cơ chế Port Knocking. Bạn chỉ cần mở cổng xác thực (Auth Port), các cổng truyền tải dữ liệu (KCP/QUIC) sẽ được Daemon tự động mở/đóng ẩn danh cho từng IP Client.

```bash
sudo ufw enable
# Mở cổng xác thực mặc định (UDP 5555)
sudo ufw allow 5555/udp
# Reload lại tường lửa
sudo ufw reload

```

**Cấp quyền Sudo không cần mật khẩu cho UFW:**
Để Daemon C++ có thể tự động đóng/mở port mà không bị chặn, bạn cần cấp quyền cho user chạy service (vd: `your_ubuntu_user`):

```bash
sudo visudo
# Thêm dòng sau vào cuối file (thay 'your_ubuntu_user' bằng user của bạn):
your_ubuntu_user ALL=(ALL) NOPASSWD: /usr/sbin/ufw, /usr/sbin/nft, /usr/bin/pkill

```

#### 3. Chuẩn bị File Cấu hình & Chứng chỉ TLS

Tạo thư mục dự án chuẩn tại `/home/<user>/zhiauth`.

```bash
mkdir -p ~/zhiauth/config ~/zhiauth/database

```

**Tạo config.json:**
Copy file `config/config.json.example` thành `~/zhiauth/config/config.json` và điền thông tin bảo mật của riêng bạn:

```json
{
  "network": {
    "auth_port": 5555,
    "quic_data_port": 4433,
    "kcp_data_port": 6666,
    "custom_mtu": 1350
  },
  "kcp_tuning": {
    "nodelay": 1,
    "interval": 1,
    "resend": 2,
    "nc": 1,
    "snd_wnd": 4096,
    "rcv_wnd": 4096
  },
  "paths": {
    "safe_root": "/export/HDD_merge",
    "log_path": "/tmp/zhiauth_gateway.log",
    "tls_crt": "config/your_domain.crt",
    "tls_key": "config/your_domain.key",
    "database": "database/zhiauth.db"
  },
  "security": {
    "master_sym_key": "REPLACE_WITH_YOUR_32_BYTE_SECRET_KEY",
    "hash_salt": "REPLACE_WITH_YOUR_RANDOM_SALT",
    "system_admin_user": "your_ubuntu_user",
    "max_fail_attempts": 5,
    "ban_duration_minutes": 15
  }
}

```

**Chứng chỉ TLS (Dành cho MsQUIC):**
Đặt 2 file chứng chỉ `.crt` và `.key` vào thư mục `~/zhiauth/config/`. Đảm bảo tên file khớp với cấu hình trong `config.json`.

#### 4. Thiết lập Systemd Service

Để ZhiAuth Server tự động chạy ngầm và khởi động cùng hệ thống, tạo file service:

```bash
sudo nano /etc/systemd/system/zhiauth.service

```

Dán cấu hình sau vào (thay `your_ubuntu_user` bằng user thực tế):

```ini
[Unit]
Description=ZhiAuth High-Performance VFS Server
After=network.target

[Service]
Type=simple
User=your_ubuntu_user
Group=your_ubuntu_user
WorkingDirectory=/home/your_ubuntu_user/zhiauth
ExecStart=/usr/local/bin/zhiauth_server_app
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target

```

Kích hoạt và khởi chạy service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable zhiauth
sudo systemctl start zhiauth
sudo systemctl status zhiauth

```

---

## 🇬🇧 English

### 📖 Overview

**Transfer_Protocol (ZhiAuth)** is a high-performance data transport and authentication ecosystem combining **C++ (Core Engine)** and **Go (Networking & Bridge)**. Built on a secure Client-Server architecture, it features end-to-end encryption, UDP transport optimization via KCP/QUIC, NAT traversal, and Virtual File System (VFS / SAF) mounting capabilities across Windows, Linux, and Android platforms.

---

### 🌿 Git Branch Structure

| Branch | Component | Description |
| --- | --- | --- |
| **`main_server`** | **Ubuntu Server** | Central server (`zhiauth_gateway`), handling authentication, session routing, KCP/QUIC Server Engine, VFS/NFS backend, and KCP receive window tuning. |
| **`Client_Windows`** | **Windows Client** | Windows client core integrating C++ Core, Go Bridge (CGO), WinFSP/WebDAV virtual disk mounting, adaptive MTU radar (`huang_mtu_radar`), and hardware fingerprinting. |
| **`Client_Linux`** | **Linux Client** | Linux client supporting FUSE/VFS mounting, KCP/QUIC tunneling, and background daemon integration. |
| **`Client_Android`** | **Android Client** | Android mobile client built with Jetpack Compose, C++ NDK (`zhiauth_jni`, Libsodium, KCP), Go Mobile (`quicdroid.aar`), and Android Storage Access Framework (`HuangDocumentsProvider`). |

---

### ✨ Key Features

#### 1. Server Engine (`main_server`)

* **Gateway & Authentication (`zhiauth_gateway`)**: Session management, client authentication, and connection dispatching.
* **KCP & QUIC Server Engine**: Optimized UDP window configurations (`IKCP_WND_RCV`) for low-latency and high-throughput transfer.
* **VFS Backend**: Remote file system query processing and storage abstraction.

#### 2. Windows Client (`Client_Windows`)

* **C++ Core & Go CGO Bridge**: High-performance C++/Go interop layer.
* **Virtual Drive Mounting**: Mounts virtual network drives into Windows Explorer via **WinFSP** or **WebDAV**.
* **MTU Radar & Hardware Identity**: Dynamic MTU detection (`huang_mtu_radar`) and hardware fingerprint generation (`hw_fingerprint`).

#### 3. Linux Client (`Client_Linux`)

* **FUSE Integration**: Seamless virtual file system mounting directly onto Linux directory trees.
* **QUIC & KCP Tunneling**: Secure, low-latency encrypted tunnel establishment to the Ubuntu Server.

#### 4. Android Client (`Client_Android`)

* **Kotlin Compose & Native NDK**: Modern Jetpack Compose UI powered by C++ NDK (`zhiauth_jni`, Libsodium, KCP) for peak cryptographic and transfer performance.
* **Go Mobile Integration**: Embedded `quicdroid.aar` compiled via Go Mobile for resilient QUIC protocol execution on mobile devices.
* **Storage Access Framework (SAF)**: Native `HuangDocumentsProvider` implementation for browsing and streaming remote files inside the system Files picker.

---

### 🛠️ Server Build & Deployment Guide

#### 1. Build Server (Ubuntu Linux)

*Prerequisites:* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`, `cmake`.

```bash
git checkout main_server

# Create build directory and compile C++ Core
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Move binaries to system directory
sudo cp zhiauth_server_app /usr/local/bin/
sudo cp zhiauth_kcp_worker /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_server_app
sudo chmod +x /usr/local/bin/zhiauth_kcp_worker
cd ..

```

#### 2. Firewall Configuration (UFW)

ZhiAuth implements a dynamic Port Knocking mechanism. You only need to open the Authentication Port; the data transfer ports (KCP/QUIC) will be dynamically managed by the Daemon per authenticated Client IP.

```bash
sudo ufw enable
# Open default Auth Port (UDP 5555)
sudo ufw allow 5555/udp
sudo ufw reload

```

**Grant Passwordless Sudo for UFW:**
The C++ Daemon requires passwordless sudo access to specific network commands to manipulate the firewall dynamically.

```bash
sudo visudo
# Append the following line (replace 'your_ubuntu_user' with your actual username):
your_ubuntu_user ALL=(ALL) NOPASSWD: /usr/sbin/ufw, /usr/sbin/nft, /usr/bin/pkill

```

#### 3. Configuration & TLS Certificates Preparation

Create the standard project directory at `/home/<user>/zhiauth`.

```bash
mkdir -p ~/zhiauth/config ~/zhiauth/database

```

**Setup config.json:**
Copy `config/config.json.example` to `~/zhiauth/config/config.json` and configure your own secret keys:

```json
{
  "network": {
    "auth_port": 5555,
    "quic_data_port": 4433,
    "kcp_data_port": 6666,
    "custom_mtu": 1350
  },
  "kcp_tuning": {
    "nodelay": 1,
    "interval": 1,
    "resend": 2,
    "nc": 1,
    "snd_wnd": 4096,
    "rcv_wnd": 4096
  },
  "paths": {
    "safe_root": "/export/HDD_merge",
    "log_path": "/tmp/zhiauth_gateway.log",
    "tls_crt": "config/your_domain.crt",
    "tls_key": "config/your_domain.key",
    "database": "database/zhiauth.db"
  },
  "security": {
    "master_sym_key": "REPLACE_WITH_YOUR_32_BYTE_SECRET_KEY",
    "hash_salt": "REPLACE_WITH_YOUR_RANDOM_SALT",
    "system_admin_user": "your_ubuntu_user",
    "max_fail_attempts": 5,
    "ban_duration_minutes": 15
  }
}

```

**TLS Certificates (For MsQUIC):**
Place your `.crt` and `.key` files in `~/zhiauth/config/`. Ensure the filenames match the configuration in `config.json`.

#### 4. Systemd Service Setup

Run ZhiAuth Server as a background daemon that starts on boot:

```bash
sudo nano /etc/systemd/system/zhiauth.service

```

Insert the following configuration (replace `your_ubuntu_user` with your actual username):

```ini
[Unit]
Description=ZhiAuth High-Performance VFS Server
After=network.target

[Service]
Type=simple
User=your_ubuntu_user
Group=your_ubuntu_user
WorkingDirectory=/home/your_ubuntu_user/zhiauth
ExecStart=/usr/local/bin/zhiauth_server_app
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target

```

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable zhiauth
sudo systemctl start zhiauth
sudo systemctl status zhiauth

```

---

## 🇨🇳 中文

### 📖 项目简介

**Transfer_Protocol (ZhiAuth)** 是一个结合了 **C++（核心引擎）** 与 **Go（网络与桥接模块）** 的高性能数据传输与身份验证生态系统。系统采用高安全性的 Client-Server 架构，具备端到端加密、基于 KCP/QUIC 的 UDP 传输优化、NAT 穿透能力，并支持在 Windows、Linux 以及 Android 平台上挂载虚拟文件系统 (VFS / SAF)。

---

### 🌿 Git 分支结构

| 分支 (Branch) | 系统组件 | 说明 |
| --- | --- | --- |
| **`main_server`** | **Ubuntu 服务端** | 核心服务端 (`zhiauth_gateway`)，负责身份验证、会话路由、KCP/QUIC 服务端引擎、VFS/NFS 后端以及 KCP 接收窗口优化。 |
| **`Client_Windows`** | **Windows 客户端** | Windows 客户端，集成 C++ Core、Go Bridge (CGO)、WinFSP/WebDAV 虚拟盘符挂载、自适应 MTU 探测 (`huang_mtu_radar`) 及硬件指纹识别。 |
| **`Client_Linux`** | **Linux 客户端** | Linux 客户端，支持 FUSE/VFS 挂载、KCP/QUIC 加密隧道及后台 Daemon 部署。 |
| **`Client_Android`** | **Android 客户端** | Android 移动客户端，基于 Jetpack Compose、C++ NDK (`zhiauth_jni`, Libsodium, KCP) 和 Go Mobile (`quicdroid.aar`) 构建，并集成 Storage Access Framework (`HuangDocumentsProvider`)。 |

---

### ✨ 核心功能

#### 1. 服务端核心 (`main_server`)

* **网关与身份验证 (`zhiauth_gateway`)**：管理客户端登录会话、权限验证与连接调度。
* **KCP & QUIC 服务端引擎**：深度优化 UDP 传输窗口 (`IKCP_WND_RCV`)，有效降低延迟与网络拥堵。
* **VFS 后端**：高效处理来自远程客户端的文件读写与存储抽象请求。

#### 2. Windows 客户端 (`Client_Windows`)

* **C++ Core & Go CGO Bridge**：C++ 与 Go 语言之间的高性能跨语言调用桥接。
* **虚拟盘符挂载**：通过 **WinFSP** 或 **WebDAV** 将远程存储直接挂载至 Windows 资源管理器。
* **MTU 探测与硬件指纹**：支持网络 MTU 动态探测 (`huang_mtu_radar`) 与本地硬件指纹生成 (`hw_fingerprint`)。

#### 3. Linux 客户端 (`Client_Linux`)

* **FUSE 文件系统集成**：通过 FUSE 将虚拟文件系统无缝挂载至 Linux 目录树。
* **QUIC & KCP 隧道**：建立至 Ubuntu 服务端的高安全、低延迟加密传输通道。

#### 4. Android 客户端 (`Client_Android`)

* **Kotlin Compose & Native NDK**：采用 Jetpack Compose 现代 UI，结合 C++ NDK 引擎 (`zhiauth_jni`, Libsodium, KCP) 实现极致加解密与传输性能。
* **Go Mobile 集成**：内嵌 Go Mobile 模块 (`quicdroid.aar`)，为移动端环境提供稳定的 QUIC 传输支持。
* **Storage Access Framework (SAF)**：内置 `HuangDocumentsProvider`，支持在 Android 系统原生“文件”管理器中直接挂载与管理远程文件。

---

### 🛠️ 服务端编译与部署指南

#### 1. 编译服务端 (Ubuntu Linux)

*环境要求：* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`, `cmake`。

```bash
git checkout main_server

# 创建构建目录并编译 C++ Core
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 移动二进制文件至系统目录
sudo cp zhiauth_server_app /usr/local/bin/
sudo cp zhiauth_kcp_worker /usr/local/bin/
sudo chmod +x /usr/local/bin/zhiauth_server_app
sudo chmod +x /usr/local/bin/zhiauth_kcp_worker
cd ..

```

#### 2. 配置防火墙 (UFW)

ZhiAuth 采用动态端口敲门（Port Knocking）机制。您只需开放身份验证端口，数据传输端口（KCP/QUIC）将由后台程序根据已验证的客户端 IP 动态开启/关闭。

```bash
sudo ufw enable
# 开放默认身份验证端口 (UDP 5555)
sudo ufw allow 5555/udp
sudo ufw reload

```

**为 UFW 配置免密 Sudo 权限：**
为了让 C++ 后台程序能够动态操作防火墙，需赋予运行用户特定命令的免密权限。

```bash
sudo visudo
# 在文件末尾添加以下内容（请将 'your_ubuntu_user' 替换为实际用户名）：
your_ubuntu_user ALL=(ALL) NOPASSWD: /usr/sbin/ufw, /usr/sbin/nft, /usr/bin/pkill

```

#### 3. 准备配置文件与 TLS 证书

在 `/home/<user>/zhiauth` 创建标准项目目录。

```bash
mkdir -p ~/zhiauth/config ~/zhiauth/database

```

**设置 config.json：**
复制 `config/config.json.example` 为 `~/zhiauth/config/config.json` 并填写您自己的安全密钥：

```json
{
  "network": {
    "auth_port": 5555,
    "quic_data_port": 4433,
    "kcp_data_port": 6666,
    "custom_mtu": 1350
  },
  "kcp_tuning": {
    "nodelay": 1,
    "interval": 1,
    "resend": 2,
    "nc": 1,
    "snd_wnd": 4096,
    "rcv_wnd": 4096
  },
  "paths": {
    "safe_root": "/export/HDD_merge",
    "log_path": "/tmp/zhiauth_gateway.log",
    "tls_crt": "config/your_domain.crt",
    "tls_key": "config/your_domain.key",
    "database": "database/zhiauth.db"
  },
  "security": {
    "master_sym_key": "REPLACE_WITH_YOUR_32_BYTE_SECRET_KEY",
    "hash_salt": "REPLACE_WITH_YOUR_RANDOM_SALT",
    "system_admin_user": "your_ubuntu_user",
    "max_fail_attempts": 5,
    "ban_duration_minutes": 15
  }
}

```

**TLS 证书（用于 MsQUIC）：**
将 `.crt` 和 `.key` 证书文件放入 `~/zhiauth/config/` 目录。请确保文件名与 `config.json` 中的配置一致。

#### 4. 使用 Systemd 运行服务

将 ZhiAuth 设置为开机自启的后台守护进程：

```bash
sudo nano /etc/systemd/system/zhiauth.service

```

粘贴以下配置内容（请将 `your_ubuntu_user` 替换为实际用户名）：

```ini
[Unit]
Description=ZhiAuth High-Performance VFS Server
After=network.target

[Service]
Type=simple
User=your_ubuntu_user
Group=your_ubuntu_user
WorkingDirectory=/home/your_ubuntu_user/zhiauth
ExecStart=/usr/local/bin/zhiauth_server_app
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target

```

启用并启动服务：

```bash
sudo systemctl daemon-reload
sudo systemctl enable zhiauth
sudo systemctl start zhiauth
sudo systemctl status zhiauth

```
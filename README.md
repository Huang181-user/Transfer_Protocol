```markdown
# 🚀 Transfer Protocol / ZhiAuth Ecosystem

[ 🇻🇳 **Tiếng Việt**](#-tiếng-việt) | [ 🇬🇧 **English**](#-english) | [ 🇨🇳 **中文**](#-中文)

---

## 🇻🇳 Tiếng Việt

### 📖 Giới thiệu
**Transfer_Protocol (ZhiAuth)** là hệ sinh thái truyền tải dữ liệu và xác thực hiệu năng cao, kết hợp giữa **C++ (Core Engine)** và **Go (Networking & Bridge)**. Hệ thống được thiết kế theo kiến trúc Client-Server bảo mật cao, hỗ trợ mã hóa đầu-cuối, tối ưu giao thức truyền tải UDP qua KCP/QUIC, vượt NAT và hỗ trợ gắn hệ thống tệp ảo (VFS) trên cả Windows và Linux.

---

### 🌿 Cấu trúc nhánh Git (Branches)

| Nhánh (Branch) | Thành phần | Mô tả chi tiết |
| :--- | :--- | :--- |
| **`main_server`** | **Ubuntu Server** | Máy chủ trung tâm (`zhiauth_gateway`), xử lý xác thực phiên, điều phối kết nối, KCP/QUIC Server Engine, backend VFS/NFS và tối ưu hóa cửa sổ truyền tải KCP. |
| **`Client_Windows`** | **Windows Client** | Client chạy trên Windows, tích hợp C++ Core, Go Bridge (CGO), gắn ổ đĩa ảo qua WinFSP/WebDAV, tự động đo MTU (`huang_mtu_radar`) và lấy định danh phần cứng. |
| **`Client_Linux`** | **Linux Client** | Client chạy trên Linux, hỗ trợ FUSE/VFS, giao thức KCP/QUIC Tunneling, tương thích daemon/systemd. |

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

---

### 🛠️ Hướng dẫn biên dịch & Cài đặt

#### 🟢 1. Biên dịch Server (Ubuntu Linux)
*Yêu cầu:* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`.

```bash
# Checkout sang branch server
git checkout main_server

# Tạo file cấu hình từ mẫu (nếu chưa có)
cp config/config.json.example config/config.json

# Biên dịch Server Engine và Gateway
make build
# Hoặc biên dịch thủ công module Go:
cd go_client && go build -o zhiauth_gateway main.go

```

#### 🟡 2. Biên dịch Windows Client

*Yêu cầu:* Windows 10/11, MSVC/MinGW C++, Go (>= 1.22), WinFSP (tùy chọn).

```powershell
# Checkout sang branch Client_Windows
git checkout Client_Windows

# Chạy script biên dịch tự động
.\build.ps1

```

#### 🔵 3. Biên dịch Linux Client

*Yêu cầu:* Ubuntu/Debian/Arch Linux, GCC/G++, Go (>= 1.22), `libfuse-dev`.

```bash
# Checkout sang branch Client_Linux
git checkout Client_Linux

# Biên dịch Client
make client

```

---

## en English

### 📖 Overview

**Transfer_Protocol (ZhiAuth)** is a high-performance data transport and authentication ecosystem combining **C++ (Core Engine)** and **Go (Networking & Bridge)**. Built on a secure Client-Server architecture, it features end-to-end encryption, UDP transport optimization via KCP/QUIC, NAT traversal, and Virtual File System (VFS) mounting capabilities across both Windows and Linux platforms.

---

### 🌿 Git Branch Structure

| Branch | Component | Description |
| --- | --- | --- |
| **`main_server`** | **Ubuntu Server** | Central server (`zhiauth_gateway`), handling authentication, session routing, KCP/QUIC Server Engine, VFS/NFS backend, and KCP receive window tuning. |
| **`Client_Windows`** | **Windows Client** | Windows client core integrating C++ Core, Go Bridge (CGO), WinFSP/WebDAV virtual disk mounting, adaptive MTU radar (`huang_mtu_radar`), and hardware fingerprinting. |
| **`Client_Linux`** | **Linux Client** | Linux client supporting FUSE/VFS mounting, KCP/QUIC tunneling, and background daemon integration. |

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

---

### 🛠️ Build & Installation

#### 🟢 1. Build Server (Ubuntu Linux)

*Prerequisites:* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`.

```bash
git checkout main_server
cp config/config.json.example config/config.json
make build

```

#### 🟡 2. Build Windows Client

*Prerequisites:* Windows 10/11, MSVC/MinGW C++, Go (>= 1.22), WinFSP (optional).

```powershell
git checkout Client_Windows
.\build.ps1

```

#### 🔵 3. Build Linux Client

*Prerequisites:* Ubuntu/Debian/Arch Linux, GCC/G++, Go (>= 1.22), `libfuse-dev`.

```bash
git checkout Client_Linux
make client

```

---

## 🇨🇳 中文

### 📖 项目简介

**Transfer_Protocol (ZhiAuth)** 是一个结合了 **C++（核心引擎）** 与 **Go（网络与桥接模块）** 的高性能数据传输与身份验证生态系统。系统采用高安全性的 Client-Server 架构，具备端到端加密、基于 KCP/QUIC 的 UDP 传输优化、NAT 穿透能力，并支持在 Windows 和 Linux 平台上挂载虚拟文件系统 (VFS)。

---

### 🌿 Git 分支结构

| 分支 (Branch) | 系统组件 | 说明 |
| --- | --- | --- |
| **`main_server`** | **Ubuntu 服务端** | 核心服务端 (`zhiauth_gateway`)，负责身份验证、会话路由、KCP/QUIC 服务端引擎、VFS/NFS 后端以及 KCP 接收窗口优化。 |
| **`Client_Windows`** | **Windows 客户端** | Windows 客户端，集成 C++ Core、Go Bridge (CGO)、WinFSP/WebDAV 虚拟盘符挂载、自适应 MTU 探测 (`huang_mtu_radar`) 及硬件指纹识别。 |
| **`Client_Linux`** | **Linux 客户端** | Linux 客户端，支持 FUSE/VFS 挂载、KCP/QUIC 加密隧道及后台 Daemon 部署。 |

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

---

### 🛠️ 编译与构建

#### 🟢 1. 编译服务端 (Ubuntu Linux)

*环境要求：* Ubuntu Server, GCC/G++, Go (>= 1.22), `make`。

```bash
git checkout main_server
cp config/config.json.example config/config.json
make build

```

#### 🟡 2. 编译 Windows 客户端

*环境要求：* Windows 10/11, MSVC/MinGW C++, Go (>= 1.22), WinFSP (可选)。

```powershell
git checkout Client_Windows
.\build.ps1

```

#### 🔵 3. 编译 Linux 客户端

*环境要求：* Ubuntu/Debian/Arch Linux, GCC/G++, Go (>= 1.22), `libfuse-dev`。

```bash
git checkout Client_Linux
make client

```

```

```

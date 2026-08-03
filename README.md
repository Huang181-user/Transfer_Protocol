<a id="top"></a>

<div align="center">

# 🚀 ZhiAuth Hybrid Dual-Mount Client v6.0
### (`Client_Linux` Branch)

**High-Performance, Zero-Latency C++/Go VFS Client for Linux Environment**

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-emerald.svg)](https://en.cppreference.com/w/cpp/20)
[![Go Version](https://img.shields.io/badge/Go-1.25%2B-00ADD8.svg)](https://golang.org/)
[![FUSE Engine](https://img.shields.io/badge/Go--FUSE-v2.5.1-orange.svg)](https://github.com/hanwen/go-fuse)

<br/>

**[ 🇻🇳 Tiếng Việt ](#vietnamese) &nbsp;•&nbsp; [ 🇬🇧 English ](#english) &nbsp;•&nbsp; [ 🇨🇳 中文 ](#chinese)**

</div>

---

<a id="vietnamese"></a>
## 🇻🇳 Tiếng Việt

<a id="vietnamese-overview"></a>
### 1. Tổng quan Dự án
**ZhiAuth Client Linux v6.0** là ứng dụng Client Mount ổ đĩa ảo (VFS) siêu tốc dành riêng cho hệ điều hành Linux. Ứng dụng được đúc theo kiến trúc **Hybrid C++/Go qua CGO**, kết nối song hành hai trục truyền tải đến ZhiAuth Server:
* **Trục chính KCP UDP (C++ Engine)**: Đảm nhận truyền tải dữ liệu RPC/VFS tốc độ cực đại, độ trễ tiệm cận 0ms.
* **Trục ngầm QUIC VFS (Go Engine)**: Đảm nhận duy trì Session, nhận cảnh báo từ Server và dự phòng khi KCP gặp sự cố.

---

<a id="vietnamese-features"></a>
### 2. Tính năng Cốt lõi
* **Tiếp nhận Port Động (Dynamic Port Binding)**: Tự động chẻ chuỗi phản hồi từ Server (`AUTH_SUCCESS|Path|QuicPort|KcpPort`) để gán Port KCP/QUIC thời gian thực, không cần hardcode cấu hình Port trên Client.
* **Thuật toán Trinh sát MTU (Huang Heuristic Radar)**: Dò tìm trần MTU vật lý mạng WAN/LAN (1000 - 1500 bytes) theo nấc thang Trăm -> Chục -> Đơn vị, chống phân mảnh gói tin tuyệt đối.
* **Động cơ KCP Turbo & Blocking UDP Receiver**: Nâng kích thước cửa sổ nhận (`WND_RCV = 4096`), kích hoạt cơ chế rút cạn bộ đệm UDP OS bằng vòng lặp Blocking `receive_from()`, trị dứt điểm bệnh sặc nước/tràn đệm khi duyệt thư mục nặng.
* **RAM Cache Metadata Zero-Latency**: Tự động lưu bộ đệm thuộc tính file/thư mục trên RAM (60 giây), giúp các lệnh `ls` hoặc thao tác trên File Manager (Thunar, Dolphin) phản hồi tức thì.
* **Dấu vân tay Phần cứng (Hardware Fingerprint Binding)**: Trích xuất địa chỉ MAC card mạng vật lý gửi kèm luồng Auth để chống đánh cắp Token Session.
* **Kiểm soát Luồng FUSE An toàn**: Bóp họng FUSE Chunk Size (`MaxWrite: 32KB`) và giới hạn `MaxBackground: 4` luồng đồng thời, triệt tiêu hoàn toàn hiện tượng treo cứng File Explorer.

---

<a id="vietnamese-structure"></a>
### 3. Sơ đồ Thư mục Nhánh `Client_Linux`

```

zhiauth_client/
├── build_client.sh              # Script dọn dẹp, biên dịch và nổ máy Client 1-click
├── CMakeLists.txt               # Cấu hình đúc libzhiauth_client_core.a
├── config/
│   └── config.json              # Thông số IP Server, Auth Port & Master Key
├── go_client/                   # Tháp điều khiển Go & Driver FUSE
│   ├── main.go                  # Luồng chính khởi tạo, MTU Radar & Terminal Scan
│   ├── cgo_wrapper.go           # Cầu nối CGO, sinh Request ID & ghép ClientID 64-bit
│   ├── config_handler.go        # Nạp cấu hình JSON rút gọn
│   ├── fuse_driver.go           # Driver Go-FUSE Dual-Mount (VFS_DRIVE & QUIC_DRIVE)
│   ├── fuse_cache.go            # Hệ thống bộ nhớ đệm RAM Cache đa luồng an toàn
│   ├── huang_mtu_radar.go       # Thuật toán dò tìm leo thang MTU động
│   ├── hw_fingerprint.go        # Quét mã MAC phần cứng
│   ├── ip_discoverer.go         # Tự động nhận diện LAN IP / Tailscale IP qua Sysfs
│   └── quic_tunnel.go           # Luồng kết nối QUIC, Reconnect ngầm & Bóc Port động
└── src/                         # Lõi C++ KCP Engine & Crypto
├── bridge/                  # client_bridge.cpp (Gọi hàm CGO Async Callback)
└── rpc_client/              # vfs_client.cpp, ikcp.c (WND 4096), Libsodium CryptoBox

```

---

<a id="vietnamese-deploy"></a>
### 4. Hướng dẫn Biên dịch & Chạy

#### Yêu cầu Môi trường
* Hệ điều hành Linux (Ubuntu / Debian / Arch)
* C++20 Compliant Compiler (`g++` hoặc `clang`)
* Go Compiler `v1.25+`
* Thư viện phụ thuộc: `cmake`, `libsodium-dev`, `libfuse3-dev` hoặc `fuse`

#### Cấu hình Client (`config/config.json`)
```json
{
  "auth_port": "5555",
  "local_port": "9090",
  "server_lan_ip": "192.168.1.83",
  "server_ts_ip": "100.125.141.48",
  "sni_domain": "zhiserver.tailc979c1.ts.net",
  "custom_mtu": 1200,
  "master_sym_key": "ZhiAuth_Secret_KCP_Key_2026_1234"
}

```

#### Thao tác 1-Click Biên dịch và Nổ máy Client

```bash
# Clone nhánh Client_Linux về máy
git clone -b Client_Linux [https://github.com/Huang181-user/Transfer_Protocol.git](https://github.com/Huang181-user/Transfer_Protocol.git) ~/zhiauth_client
cd ~/zhiauth_client

# Cấp quyền thực thi và chạy script tự động
chmod +x build_client.sh
./build_client.sh

```

[⬆ Về đầu trang](https://www.google.com/search?q=%23top)

---


<a id="english"></a>
## 🇬🇧 English

### 1. Overview

**ZhiAuth Client Linux v6.0** is a high-speed Virtual File System (VFS) mount client designed specifically for Linux operating systems. Built with a **Hybrid C++/Go Architecture via CGO**, it establishes dual concurrent transmission tunnels to the ZhiAuth Server:

* **Primary KCP UDP Tunnel (C++ Engine)**: Handles RPC/VFS data transport with maximum throughput and near-zero latency.
* **Fallback QUIC VFS Tunnel (Go Engine)**: Maintains session state, listens for server termination signals, and acts as a failover tunnel.

---

### 2. Key Features

* **Dynamic Port Binding**: Automatically parses server authentication payloads (`AUTH_SUCCESS|Path|QuicPort|KcpPort`) to bind KCP/QUIC sockets dynamically without hardcoded data ports.
* **Huang MTU Heuristic Radar**: Probes network WAN/LAN MTU limits (1000 - 1500 bytes) via a stepwise search (Hundreds -> Tens -> Units), preventing packet fragmentation.
* **KCP Turbo & Blocking UDP Receiver**: Expanded receive window (`WND_RCV = 4096`) combined with a blocking UDP `recvfrom()` loop to drain OS socket buffers instantaneously and prevent I/O freezes.
* **Zero-Latency RAM Metadata Cache**: Thread-safe in-memory caching for file attributes (60s TTL), providing instant responses for file managers (Thunar, Dolphin, etc.).
* **Hardware Fingerprint Binding**: Extracts physical network interface MAC addresses during authentication to prevent token theft and session hijacking.
* **Throttled FUSE Stream Control**: Capped chunk sizes (`MaxWrite: 32KB`) and concurrency limit (`MaxBackground: 4`) to eliminate File Explorer unresponsive states during heavy directory scans.

---

### 3. Directory Structure (`Client_Linux` Branch)

```
zhiauth_client/
├── build_client.sh              # One-click build and execution script
├── CMakeLists.txt               # CMake configuration for libzhiauth_client_core.a
├── config/
│   └── config.json              # Server IP, Auth Port, and Master Key configuration
├── go_client/                   # Go Control Tower & FUSE Engine
│   ├── main.go                  # Main entry point, MTU Radar & terminal loop
│   ├── cgo_wrapper.go           # CGO Interop, Request ID generator & 64-bit session packer
│   ├── config_handler.go        # Lean JSON config loader
│   ├── fuse_driver.go           # Go-FUSE Dual-Mount Driver (VFS_DRIVE & QUIC_DRIVE)
│   ├── fuse_cache.go            # Thread-safe in-memory RAM metadata cache
│   ├── huang_mtu_radar.go       # Dynamic MTU heuristic search algorithm
│   ├── hw_fingerprint.go        # Hardware MAC fingerprint extractor
│   ├── ip_discoverer.go         # Kernel Sysfs LAN/Tailscale IP detector
│   └── quic_tunnel.go           # QUIC connection manager & dynamic port parser
└── src/                         # C++ KCP Engine Core & Libsodium Crypto
    ├── bridge/                  # client_bridge.cpp (Async CGO callback forwarder)
    └── rpc_client/              # vfs_client.cpp, ikcp.c (WND 4096), Libsodium CryptoBox

```

---

### 4. Build & Usage Instructions

#### Prerequisites

* Linux OS (Ubuntu / Debian / Arch)
* C++20 Compliant Compiler (`g++` or `clang`)
* Go Compiler `v1.25+`
* Dependencies: `cmake`, `libsodium-dev`, `libfuse3-dev` or `fuse`

#### One-Click Build & Run

```bash
# Clone the Client_Linux branch
git clone -b Client_Linux [https://github.com/Huang181-user/Transfer_Protocol.git](https://github.com/Huang181-user/Transfer_Protocol.git) ~/zhiauth_client
cd ~/zhiauth_client

# Grant execution rights and run the build script
chmod +x build_client.sh
./build_client.sh

```

[⬆ Back to Top](https://www.google.com/search?q=%23top)

---


<a id="chinese"></a>
## 🇨🇳 中文

### 1. 项目概述

**ZhiAuth Client Linux v6.0** 是专为 Linux 操作系统打造的高速虚拟文件系统 (VFS) 挂载客户端。项目基于 **C++/Go 混合架构 (通过 CGO 交互)**，向 ZhiAuth 服务端建立双通道并行连接：

* **主通道 KCP UDP (C++ 引擎)**：负责 RPC/VFS 数据的高吞吐量、极低延迟传输。
* **备用通道 QUIC VFS (Go 引擎)**：负责维持 Session 状态、接收服务端下发的控制信号，并在 KCP 异常时作为备用数据通道。

---

### 2. 核心特性

* **动态端口绑定 (Dynamic Port Binding)**：自动解析服务端认证返回的 `AUTH_SUCCESS|Path|QuicPort|KcpPort` 字符串，实时绑定数据端口，无需在客户端硬编码数据端口。
* **黄氏 MTU 启发式雷达 (Huang MTU Heuristic Radar)**：按照“百 -> 十 -> 个位”三级阶梯探测 WAN/LAN MTU 极限 (1000 - 1500 bytes)，彻底杜绝 UDP 包分片。
* **KCP Turbo 与阻塞式 UDP 接收器**：将接收窗口扩大至 `WND_RCV = 4096`，并结合阻塞式 `recvfrom()` 循环瞬间抽干 OS Socket 缓冲区，彻底解决高并发下的丢包与卡顿。
* **零延迟 RAM 元数据缓存**：线程安全的内存缓存 (60 秒 TTL)，为文件管理器 (Thunar, Dolphin 等) 提供即时目录响应。
* **硬件指纹绑定 (Hardware Fingerprint)**：提取物理网卡 MAC 地址参与认证，防止 Token 被盗用或会话劫持。
* **FUSE 流量平滑控制**：限制单次 Chunk 大小 (`MaxWrite: 32KB`) 并限制并发数 (`MaxBackground: 4`)，防止文件管理器刷缩略图时引发网络拥塞。

---

### 3. `Client_Linux` 分支目录结构

```
zhiauth_client/
├── build_client.sh              # 一键编译并运行客户端脚本
├── CMakeLists.txt               # 编译 libzhiauth_client_core.a 的 CMake 配置
├── config/
│   └── config.json              # 服务端 IP、认证端口及主密钥配置
├── go_client/                   # Go 控制塔与 FUSE 挂载引擎
│   ├── main.go                  # 主入口、MTU 雷达及终端交互
│   ├── cgo_wrapper.go           # CGO 桥接、Request ID 生成器及 64 位 Session 打包
│   ├── config_handler.go        # 精简版 JSON 配置加载器
│   ├── fuse_driver.go           # Go-FUSE 双挂载驱动 (VFS_DRIVE 与 QUIC_DRIVE)
│   ├── fuse_cache.go            # 线程安全的内存 RAM 元数据缓存
│   ├── huang_mtu_radar.go       # 动态 MTU 启发式探测算法
│   ├── hw_fingerprint.go        # 硬件 MAC 指纹提取器
│   ├── ip_discoverer.go         # 基于 Sysfs 的 LAN/Tailscale IP 自动识别
│   └── quic_tunnel.go           # QUIC 连接管理器及动态端口解析器
└── src/                         # C++ KCP 引擎核心与 Libsodium 加密
    ├── bridge/                  # client_bridge.cpp (CGO 异步 Callback 转发)
    └── rpc_client/              # vfs_client.cpp, ikcp.c (WND 4096), Libsodium CryptoBox

```

---

### 4. 构建与使用说明

#### 环境要求

* Linux 操作系统 (Ubuntu / Debian / Arch)
* 支持 C++20 的编译器 (`g++` 或 `clang`)
* Go 语言编译器 `v1.25+`
* 依赖系统库：`cmake`, `libsodium-dev`, `libfuse3-dev` 或 `fuse`

#### 一键构建与启动

```bash
# 克隆 Client_Linux 分支代码
git clone -b Client_Linux [https://github.com/Huang181-user/Transfer_Protocol.git](https://github.com/Huang181-user/Transfer_Protocol.git) ~/zhiauth_client
cd ~/zhiauth_client

# 赋予执行权限并运行构建脚本
chmod +x build_client.sh
./build_client.sh

```

[⬆ 返回顶部](https://www.google.com/search?q=%23top)

---

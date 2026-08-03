# 🚀 Transfer Protocol / ZhiAuth Ecosystem

[ vi **Tiếng Việt**](#vietnamese) | [ en **English**](#english) | [ cn **中文**](#chinese)

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

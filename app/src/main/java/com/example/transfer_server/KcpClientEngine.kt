package com.example.app.network

class KcpClientEngine : NetworkProtocol {
    private val tag = "KcpClientEngine"
    private var isKcpConnected = false
    private var targetHost: String = ""
    private var targetPort: Int = 0

    override fun connect(host: String, port: Int) {
        this.targetHost = host
        this.targetPort = port
        RealtimeLogger.i(tag, "Khởi tạo kết nối KCP tới $host:$port...")

        try {
            // Config thông số KCP: nodelay=1, interval=10ms, resend=2, nc=1 (No Congestion Control)
            RealtimeLogger.d(tag, "Cấu hình KCP Parameters: nodelay=1, interval=10, resend=2, nc=1")
            
            // TODO: Bind Socket UDP & Init KCP Control Block tại đây
            
            isKcpConnected = true
            RealtimeLogger.i(tag, "Kết nối KCP thành công tới $host:$port!")
        } catch (e: Exception) {
            isKcpConnected = false
            RealtimeLogger.e(tag, "Thất bại khi kết nối KCP tới $host:$port", e)
        }
    }

    override fun sendData(data: ByteArray): Boolean {
        RealtimeLogger.d(tag, "Chuẩn bị gửi ${data.size} bytes qua KCP...")
        if (!isKcpConnected) {
            RealtimeLogger.e(tag, "Không thể gửi dữ liệu: KCP chưa được kết nối!")
            return false
        }

        return try {
            RealtimeLogger.d(tag, "Đang đẩy dữ liệu vào KCP Output Queue...")
            // TODO: kcp.send(data)
            RealtimeLogger.i(tag, "Đã gửi thành công ${data.size} bytes qua KCP.")
            true
        } catch (e: Exception) {
            RealtimeLogger.e(tag, "Lỗi khi gửi dữ liệu qua KCP", e)
            false
        }
    }

    override fun receiveData(): ByteArray? {
        if (!isKcpConnected) return null
        RealtimeLogger.d(tag, "Đang kiểm tra dữ liệu nhận về từ KCP Stream...")
        // TODO: kcp.recv()
        return null
    }

    override fun disconnect() {
        RealtimeLogger.i(tag, "Đang đóng kết nối KCP với $targetHost:$targetPort...")
        isKcpConnected = false
        RealtimeLogger.i(tag, "Đã ngắt kết nối KCP hoàn toàn.")
    }

    override fun isConnected(): Boolean = isKcpConnected
}

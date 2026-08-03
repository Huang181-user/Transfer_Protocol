package com.example.app.network

class NetworkManager {
    private val tag = "NetworkManager"
    private var activeEngine: NetworkProtocol? = null

    fun initialize(type: ProtocolType, host: String, port: Int) {
        RealtimeLogger.i(tag, "Khởi chạy NetworkManager với giao thức: $type (SFTP đã bị loại bỏ)")
        
        // Ngắt kết nối cũ nếu đang chạy
        activeEngine?.disconnect()

        activeEngine = when (type) {
            ProtocolType.KCP -> KcpClientEngine()
            ProtocolType.QUIC -> QuicClientEngine()
        }

        RealtimeLogger.d(tag, "Engine được chọn: ${activeEngine!!::class.java.simpleName}")
        activeEngine?.connect(host, port)
    }

    fun sendPayload(payload: ByteArray): Boolean {
        RealtimeLogger.d(tag, "Yêu cầu gửi gói tin ${payload.size} bytes...")
        if (activeEngine == null) {
            RealtimeLogger.e(tag, "Lỗi: Chưa khởi tạo Giao thức mạng! Vui lòng gọi initialize() trước.")
            return false
        }
        return activeEngine!!.sendData(payload)
    }

    fun shutdown() {
        RealtimeLogger.i(tag, "Đang tắt NetworkManager...")
        activeEngine?.disconnect()
        activeEngine = null
        RealtimeLogger.i(tag, "NetworkManager đã dừng hoàn toàn.")
    }
}

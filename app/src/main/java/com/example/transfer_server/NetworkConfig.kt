package com.example.transfer_server

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

object NetworkConfig {
    var LOCAL_IP by mutableStateOf("")
    var TS_IP by mutableStateOf("")
    var SERVER_IP by mutableStateOf("")
    var QUIC_PORT by mutableStateOf("4433")
    var AUTH_PORT by mutableStateOf("5555")
    var KCP_PORT by mutableStateOf("6666")

    var QUIC_USER by mutableStateOf("")
    var QUIC_PASS by mutableStateOf("")
    var ROOT_PATH by mutableStateOf("/")

    var QUIC_MTU by mutableStateOf(1350)
    var SNI_DOMAIN = ""
    var MASTER_SYM_KEY = ""
    var HW_ID = ""

    var KCP_NODELAY by mutableStateOf(1)
    var KCP_INTERVAL by mutableStateOf(10)
    var KCP_RESEND by mutableStateOf(2)
    var KCP_NC by mutableStateOf(1)
    var KCP_SND_WND by mutableStateOf(4096)
    var KCP_RCV_WND by mutableStateOf(4096)

    // 🔥 THUẬT TOÁN ĐUA PHÂN LUỒNG: Quét cả 2 mạng cùng lúc, mạng nào phản hồi trước múc trước!
    // 🔥 THUẬT TOÁN ĐUA PHÂN LUỒNG: Xài ICMP Ping hệ điều hành thay vì TCP Socket!
    fun discoverBestRouteSync(): String {
        var activeIp = ""
        val latch = java.util.concurrent.CountDownLatch(1)

        val checkIp = { ip: String ->
            Thread {
                try {
                    if (ip.isNotEmpty() && ip != "NONE") {
                        // Dùng lệnh ping truyền thống của Linux/Android (Chờ 1 giây)
                        val process = Runtime.getRuntime().exec("ping -c 1 -W 1 $ip")
                        val exitVal = process.waitFor()
                        if (exitVal == 0) {
                            synchronized(latch) {
                                if (activeIp.isEmpty()) { activeIp = ip; latch.countDown() }
                            }
                        }
                    }
                } catch (e: Exception) {}
            }.start()
        }

        checkIp(LOCAL_IP)
        checkIp(TS_IP)
        latch.await(1500, java.util.concurrent.TimeUnit.MILLISECONDS)

        return if (activeIp.isNotEmpty()) activeIp else TS_IP
    }
}
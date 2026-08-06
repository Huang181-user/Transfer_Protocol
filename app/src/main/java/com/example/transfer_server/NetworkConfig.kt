package com.example.transfer_server

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

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

    // 🔥 BỔ SUNG: Biến hứng 6 thông số KCP Tuning cấp phát động từ Server
    var KCP_NODELAY by mutableStateOf(1)
    var KCP_INTERVAL by mutableStateOf(10)
    var KCP_RESEND by mutableStateOf(2)
    var KCP_NC by mutableStateOf(1)
    var KCP_SND_WND by mutableStateOf(4096)
    var KCP_RCV_WND by mutableStateOf(4096)
}
package com.example.transfer_server
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

object NetworkConfig {
    var LOCAL_IP by mutableStateOf("")
    var TS_IP by mutableStateOf("")
    var SERVER_IP by mutableStateOf("")
    var QUIC_PORT by mutableStateOf("4433")

    // 3 Biến Động Nhập Từ Màn Hình
    var QUIC_USER by mutableStateOf("")
    var QUIC_PASS by mutableStateOf("")
    var ROOT_PATH by mutableStateOf("")

    var QUIC_MTU by mutableStateOf(1200)

    var SNI_DOMAIN = ""
}
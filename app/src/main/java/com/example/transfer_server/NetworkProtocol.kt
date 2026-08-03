package com.example.app.network

enum class ProtocolType {
    KCP,
    QUIC
}

interface NetworkProtocol {
    fun connect(host: String, port: Int)
    fun sendData(data: ByteArray): Boolean
    fun receiveData(): ByteArray?
    fun disconnect()
    fun isConnected(): Boolean
}

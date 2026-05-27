package com.example.transfer_server
import kotlinx.coroutines.runBlocking
import quicclient.Quicclient

object HuangTransport {
    fun getStat(path: String): String = runBlocking {
        val url = "https://${NetworkConfig.QUIC_USER}:${NetworkConfig.QUIC_PASS}@${NetworkConfig.SERVER_IP}:${NetworkConfig.QUIC_PORT}/api/stat?path=$path"
        // 🎯 Đã thêm NetworkConfig.SNI_DOMAIN
        return@runBlocking Quicclient.fetchJson(url, NetworkConfig.QUIC_MTU.toLong(), NetworkConfig.SNI_DOMAIN)
    }
    fun listFiles(path: String): String = runBlocking {
        val url = "https://${NetworkConfig.QUIC_USER}:${NetworkConfig.QUIC_PASS}@${NetworkConfig.SERVER_IP}:${NetworkConfig.QUIC_PORT}/api/list?path=$path"
        // 🎯 Đã thêm NetworkConfig.SNI_DOMAIN
        return@runBlocking Quicclient.fetchJson(url, NetworkConfig.QUIC_MTU.toLong(), NetworkConfig.SNI_DOMAIN)
    }
    fun download(remotePath: String, localPath: String): Boolean {
        val url = "https://${NetworkConfig.QUIC_USER}:${NetworkConfig.QUIC_PASS}@${NetworkConfig.SERVER_IP}:${NetworkConfig.QUIC_PORT}/download$remotePath"
        // 🎯 Đã thêm NetworkConfig.SNI_DOMAIN
        return Quicclient.downloadFast(url, localPath, NetworkConfig.QUIC_MTU.toLong(), NetworkConfig.SNI_DOMAIN).startsWith("SUCCESS")
    }
}
package com.example.transfer_server

import android.util.Log
import com.jcraft.jsch.ChannelSftp
import com.jcraft.jsch.JSch
import com.jcraft.jsch.Session
import java.util.Properties

object SftpFallbackClient {
    private val TAG = "SFTP_FALLBACK"
    private val USER = AppSecrets.SFTP_USER
    private val PASS = AppSecrets.SFTP_PASS

    fun listFilesJson(ip: String, path: String): String {
        return try {
            val jsch = JSch()
            val session = jsch.getSession(USER, ip, AppSecrets.SFTP_PORT).apply {
                setPassword(PASS)
                setConfig(Properties().apply { put("StrictHostKeyChecking", "no") })
                connect(5000)
            }
            val channel = session.openChannel("sftp") as ChannelSftp
            channel.connect(3000)
            val files = channel.ls(path)
            val sb = StringBuilder("[")
            files.forEachIndexed { i, entry ->
                val f = entry as ChannelSftp.LsEntry
                if (f.filename != "." && f.filename != "..") {
                    val fullPath = if (path.endsWith("/")) path + f.filename else "$path/${f.filename}"
                    sb.append("{\"name\":\"${f.filename}\",\"size\":${f.attrs.size},\"is_dir\":${f.attrs.isDir},\"path\":\"$fullPath\"}")
                    if (i < files.size - 1) sb.append(",")
                }
            }
            sb.append("]")
            channel.disconnect()
            session.disconnect()
            sb.toString()
        } catch (e: Exception) {
            Log.e(TAG, "❌ SFTP Oẳng: ${e.message}")
            "ERROR"
        }
    }
}

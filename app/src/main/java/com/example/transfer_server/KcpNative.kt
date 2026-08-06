package com.example.transfer_server

import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

object KcpNative {
    var remoteRoot: String = ""
    private const val TAG = "HUANG_KCP_NATIVE"

    private fun logRealtime(level: String, msg: String) {
        val sdf = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())
        val timeStr = sdf.format(Date())
        Log.i(TAG, "[$timeStr] [$level] $msg")
    }

    init {
        try {
            System.loadLibrary("zhiauth_kcp_jni")
            logRealtime("INFO", "✅ Đã nạp thành công NDK libzhiauth_kcp_jni.so!")
        } catch (e: Exception) {
            logRealtime("ERROR", "❌ Lỗi nạp C++: ${e.message}")
        }
    }

    // 🔥 CẬP NHẬT: Nhận đầy đủ 6 thông số KCP Tuning từ Server
    external fun initKcp(
        serverIp: String,
        port: Int,
        masterKey: String,
        mtu: Int,
        nodelay: Int,
        interval: Int,
        resend: Int,
        nc: Int,
        sndWnd: Int,
        rcvWnd: Int
    ): Boolean

    external fun sendRawKcp(opcode: Byte, path: String, offset: Long, reqLen: Int, data: ByteArray?): ByteArray?
    external fun shutdownKcp()

    private fun formatError(msg: String) = "{\"error\": \"$msg\"}"

    private fun resolvePath(p: String): String {
        if (remoteRoot.isEmpty()) return p
        var cleanP = p
        if (!cleanP.startsWith("/")) cleanP = "/$cleanP"
        if (cleanP == "/") return remoteRoot
        if (cleanP.startsWith(remoteRoot)) return cleanP
        return remoteRoot + if (cleanP == "/") "" else cleanP
    }

    fun vfsStat(path: String): String {
        val realPath = resolvePath(path)
        val res = sendRawKcp(0x01, realPath, 0, 0, null) ?: return formatError("Timeout hoặc Lỗi NDK")
        if (res.size < 9) return formatError("Packet too short")
        val buffer = ByteBuffer.wrap(res).order(ByteOrder.LITTLE_ENDIAN)
        val size = buffer.long
        val isDir = buffer.get().toInt() == 1

        var name = path.substringAfterLast("/")
        if (name.isEmpty() || path == "/") {
            name = if (path == "/") remoteRoot.substringAfterLast("/") else "/"
            if (name.isEmpty()) name = "/"
        }
        return "{\"name\":\"$name\", \"size\":$size, \"is_dir\":$isDir}"
    }

    fun vfsList(path: String): String {
        val realPath = resolvePath(path)
        val res = sendRawKcp(0x02, realPath, 0, 0, null) ?: return "[]"
        val cleanStr = String(res).trimEnd('\u0000').trim()
        val arr = mutableListOf<String>()
        val vSlash = if (path.endsWith("/")) path else "$path/"

        cleanStr.split("|").forEach { t ->
            if (t.isNotBlank()) {
                // 🔥 FIX: Tên file có thể chứa dấu phẩy, nên ta cắt chuỗi từ dưới lên!
                // Format C++ gửi: FileName,DIR/REG,Size,33188
                val modeIdx = t.lastIndexOf(",")
                val sizeIdx = t.lastIndexOf(",", modeIdx - 1)
                val typeIdx = t.lastIndexOf(",", sizeIdx - 1)

                if (typeIdx > 0 && sizeIdx > typeIdx && modeIdx > sizeIdx) {
                    val fileName = t.substring(0, typeIdx).replace("\"", "").trim()
                    val typeStr = t.substring(typeIdx + 1, sizeIdx).trim()
                    val sizeStr = t.substring(sizeIdx + 1, modeIdx).trim()

                    val isDir = if (typeStr == "DIR") "true" else "false"
                    val fileSize = if (sizeStr.toLongOrNull() != null) sizeStr else "0"

                    arr.add("{\"name\":\"$fileName\",\"is_dir\":$isDir,\"size\":$fileSize,\"path\":\"$vSlash$fileName\"}")
                }
            }
        }
        return "[${arr.joinToString(",")}]"
    }

    fun vfsMkdir(path: String): Boolean = sendRawKcp(0x05, resolvePath(path), 0, 0, null) != null
    fun vfsDelete(path: String): Boolean = sendRawKcp(0x06, resolvePath(path), 0, 0, null) != null
    fun vfsCreate(path: String): Boolean = sendRawKcp(0x04, resolvePath(path), 0, 0, ByteArray(0)) != null
    fun vfsWrite(path: String, offset: Long, data: ByteArray): Boolean = sendRawKcp(0x04, resolvePath(path), offset, 0, data) != null
    fun vfsRename(oldP: String, newP: String): Boolean = sendRawKcp(0x07, resolvePath(oldP), 0, 0, resolvePath(newP).toByteArray()) != null

    fun vfsDownload(remoteP: String, localP: String): Boolean {
        val realPath = resolvePath(remoteP)
        val statRes = sendRawKcp(0x01, realPath, 0, 0, null) ?: return false
        if (statRes.size < 8) return false
        val buffer = ByteBuffer.wrap(statRes).order(ByteOrder.LITTLE_ENDIAN)
        val size = buffer.long

        if (size == 0L) {
            File(localP).writeBytes(ByteArray(0))
            return true
        }

        val raf = try {
            RandomAccessFile(localP, "rw").apply { setLength(size) }
        } catch (e: Exception) {
            logRealtime("ERROR", "Lỗi tạo file RandomAccessFile: ${e.message}")
            return false
        }

        val chunkSize = 256 * 1024L
        val isSuccess = AtomicBoolean(true)
        val numThreads = 8
        val executor = Executors.newFixedThreadPool(numThreads)

        logRealtime("INFO", "🚀 Kích hoạt Tải Song Song: 8 Luồng | Kích thước: $size bytes")

        var offset = 0L
        while (offset < size) {
            val currentOffset = offset
            val reqLen = if (size - currentOffset < chunkSize) (size - currentOffset).toInt() else chunkSize.toInt()

            executor.submit {
                if (!isSuccess.get()) return@submit

                var retry = 0
                var chunkSuccess = false
                while (retry < 3 && !chunkSuccess) {
                    val data = sendRawKcp(0x03, realPath, currentOffset, reqLen, null)
                    if (data != null && data.size == reqLen) {
                        synchronized(raf) {
                            raf.seek(currentOffset)
                            raf.write(data)
                        }
                        chunkSuccess = true
                    } else {
                        retry++
                        logRealtime("WARN", "⚠️ Lỗi chunk offset $currentOffset, đang retry $retry/3...")
                    }
                }

                if (!chunkSuccess) {
                    logRealtime("ERROR", "❌ Chunk offset $currentOffset thất bại hoàn toàn sau 3 lần thử!")
                    isSuccess.set(false)
                }
            }
            offset += reqLen
        }

        executor.shutdown()
        try {
            executor.awaitTermination(1, TimeUnit.HOURS)
        } catch (e: Exception) {
            isSuccess.set(false)
        }

        try { raf.close() } catch (e: Exception) {}

        if (!isSuccess.get()) {
            File(localP).delete()
            logRealtime("ERROR", "❌ Tiến trình tải Multi-part thất bại: $localP")
        } else {
            logRealtime("INFO", "🎉 Tải song song HOÀN TẤT: $localP ($size bytes)")
        }

        return isSuccess.get()
    }
}
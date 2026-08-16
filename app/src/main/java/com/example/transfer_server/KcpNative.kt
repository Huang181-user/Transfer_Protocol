package com.example.transfer_server

import com.example.app.network.RealtimeLogger
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

object KcpNative {
    var remoteRoot: String = ""
    private const val TAG = "HUANG_KCP_NATIVE"

    // 🔥 NỐI LOG VÀO REALTIME LOGGER ĐỂ BẮN RA UI APP
    private fun logRealtime(level: String, msg: String) {
        if (level == "ERROR") {
            RealtimeLogger.e(TAG, msg)
        } else {
            RealtimeLogger.i(TAG, msg)
        }
    }

    init {
        try {
            System.loadLibrary("zhiauth_kcp_jni")
            logRealtime("INFO", "✅ Đã nạp NDK libzhiauth_kcp_jni.so!")
        } catch (e: Exception) {
            logRealtime("ERROR", "❌ Lỗi nạp C++: ${e.message}")
        }
    }

    external fun initKcp(
        serverIp: String, port: Int, masterKey: String, mtu: Int,
        nodelay: Int, interval: Int, resend: Int, nc: Int, sndWnd: Int, rcvWnd: Int
    ): Boolean

    external fun reconnectSocket()
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

        if (res.size < 37) return formatError("Packet too short (Cần 37 bytes)")

        val buffer = ByteBuffer.wrap(res).order(ByteOrder.LITTLE_ENDIAN)
        val size = buffer.long
        val isDir = buffer.get().toInt() == 1
        val mtime = buffer.long * 1000L
        val ctime = buffer.long * 1000L
        val atime = buffer.long * 1000L
        val mode = buffer.int

        var name = path.substringAfterLast("/")
        if (name.isEmpty() || path == "/") {
            name = if (path == "/") remoteRoot.substringAfterLast("/") else "/"
            if (name.isEmpty()) name = "/"
        }

        return "{\"name\":\"$name\", \"size\":$size, \"is_dir\":$isDir, \"last_modified\":$mtime, \"ctime\":$ctime, \"atime\":$atime, \"mode\":$mode}"
    }

    fun vfsList(path: String): String {
        val realPath = resolvePath(path)
        val res = sendRawKcp(0x02, realPath, 0, 0, null) ?: return "[]"
        val arr = mutableListOf<String>()
        val vSlash = if (path.endsWith("/")) path else "$path/"
        val buffer = ByteBuffer.wrap(res).order(ByteOrder.LITTLE_ENDIAN)

        while (buffer.remaining() >= 15) {
            val nameLen = buffer.short.toInt() and 0xFFFF
            val isDirByte = buffer.get().toInt()
            val size = buffer.long
            buffer.int // bypass mode

            if (buffer.remaining() < nameLen) {
                logRealtime("ERROR", "Packet hụt dữ liệu khi đang đọc tên file!")
                break
            }

            val nameBytes = ByteArray(nameLen)
            buffer.get(nameBytes)
            val fileName = String(nameBytes, Charsets.UTF_8).replace("\"", "\\\"")

            val isDirStr = if (isDirByte == 1) "true" else "false"
            arr.add("{\"name\":\"$fileName\",\"is_dir\":$isDirStr,\"size\":$size,\"path\":\"$vSlash$fileName\"}")
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
        val downloadedBytes = AtomicLong(0L) // 🔥 BIẾN LƯU SỐ BYTES ĐÃ TẢI
        val numThreads = 8
        val executor = Executors.newFixedThreadPool(numThreads)

        logRealtime("INFO", "🚀 Tải Song Song (8 Threads) | Chunk 256KB | Kích thước: $size bytes")

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

                        // 🔥 TÍNH % VÀ BẮN RA UI (CHỈ BÁO CÁO MỖI 10% ĐỂ KHÔNG SPAM RAM)
                        val totalNow = downloadedBytes.addAndGet(reqLen.toLong())
                        val percent = (totalNow * 100 / size).toInt()
                        val previousTotal = totalNow - reqLen
                        val oldPercent = (previousTotal * 100 / size).toInt()

                        if (percent / 10 > oldPercent / 10 || totalNow == size) {
                            logRealtime("INFO", "⬇️ Đang tải: $percent% ($totalNow / $size bytes)")
                        }

                    } else {
                        retry++
                        logRealtime("WARN", "⚠️ Lỗi block chunk offset $currentOffset, đang retry $retry/3...")
                    }
                }

                if (!chunkSuccess) {
                    logRealtime("ERROR", "❌ Chunk offset $currentOffset sụp đổ hoàn toàn sau 3 lần thử!")
                    isSuccess.set(false)
                }
            }
            offset += reqLen
        }

        executor.shutdown()
        try { executor.awaitTermination(1, TimeUnit.HOURS) } catch (e: Exception) { isSuccess.set(false) }
        try { raf.close() } catch (e: Exception) {}

        if (!isSuccess.get()) {
            File(localP).delete()
            logRealtime("ERROR", "❌ Tải file thất bại: $localP")
        } else {
            logRealtime("INFO", "🎉 HOÀN TẤT TẢI FILE: $localP ($size bytes)")
        }

        return isSuccess.get()
    }
}
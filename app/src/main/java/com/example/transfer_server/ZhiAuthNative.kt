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

object ZhiAuthNative {
    var remoteRoot: String = ""
    private const val TAG = "ZHIAUTH_JNI"

    init {
        try {
            System.loadLibrary("zhiauth_jni")
            RealtimeLogger.i(TAG, "✅ Đã nạp NDK libzhiauth_jni.so!")
        } catch (e: Throwable) { // 🔥 SỬA EXCEPTION THÀNH THROWABLE ĐỂ BẮT LỖI UNSATISFIED LINK ERROR
            RealtimeLogger.e(TAG, "❌ Lỗi nạp C++: ${e.message}", e)
        }
    }

    external fun discoverMtu(ip: String): Int
    external fun getDeviceIps(): String
    
    external fun initMsQuic(ip: String, authPort: Int, dataPort: Int): Boolean
    external fun authMsQuic(payload: String): String
    external fun shutdownQuic()

    external fun initKcp(serverIp: String, port: Int, masterKey: String, mtu: Int, nodelay: Int, interval: Int, resend: Int, nc: Int, sndWnd: Int, rcvWnd: Int): Boolean
    external fun sendRawKcp(opcode: Byte, path: String, offset: Long, reqLen: Int, data: ByteArray?): ByteArray?
    external fun shutdownKcp()
    external fun reconnectSocket()

    private fun resolvePath(p: String): String {
        if (remoteRoot.isEmpty()) return p
        var cleanP = p
        if (!cleanP.startsWith("/")) cleanP = "/$cleanP"
        if (cleanP == "/") return remoteRoot
        if (cleanP.startsWith(remoteRoot)) return cleanP
        return remoteRoot + if (cleanP == "/") "" else cleanP
    }

    fun vfsStat(isQuic: Boolean, path: String): String {
        // 🔥 Nếu NULL tức là Timeout hoặc bị Server từ chối (OP_ERROR)
        val res = sendRawKcp(0x01, resolvePath(path), 0, 0, null) ?: return "{\"error\": \"Không tìm thấy file hoặc Timeout\"}"
        if (res.size < 37) return "{\"error\": \"Packet too short\"}"
        val buffer = ByteBuffer.wrap(res).order(ByteOrder.LITTLE_ENDIAN)
        val size = buffer.long
        val isDir = buffer.get().toInt() == 1
        val mtime = buffer.long * 1000L
        val ctime = buffer.long * 1000L
        val atime = buffer.long * 1000L
        val mode = buffer.int
        var name = path.substringAfterLast("/")
        if (name.isEmpty() || path == "/") name = if (path == "/") remoteRoot.substringAfterLast("/") else "/"
        if (name.isEmpty()) name = "/"
        return "{\"name\":\"$name\", \"size\":$size, \"is_dir\":$isDir, \"last_modified\":$mtime, \"ctime\":$ctime, \"atime\":$atime, \"mode\":$mode}"
    }

    fun vfsList(isQuic: Boolean, path: String): String {
        val res = sendRawKcp(0x02, resolvePath(path), 0, 0, null) ?: return "[]"
        val arr = mutableListOf<String>()
        val vSlash = if (path.endsWith("/")) path else "$path/"
        val buffer = ByteBuffer.wrap(res).order(ByteOrder.LITTLE_ENDIAN)
        while (buffer.remaining() >= 39) {
            val nameLen = buffer.short.toInt() and 0xFFFF
            val isDirByte = buffer.get().toInt()
            val size = buffer.long
            val mtime = buffer.long * 1000L
            val ctime = buffer.long * 1000L
            val atime = buffer.long * 1000L
            val mode = buffer.int
            if (buffer.remaining() < nameLen) break
            val nameBytes = ByteArray(nameLen)
            buffer.get(nameBytes)
            val fileName = String(nameBytes, Charsets.UTF_8).replace("\"", "\\\"")
            val isDirStr = if (isDirByte == 1) "true" else "false"
            arr.add("{\"name\":\"$fileName\",\"is_dir\":$isDirStr,\"size\":$size,\"last_modified\":$mtime,\"ctime\":$ctime,\"atime\":$atime,\"mode\":$mode,\"path\":\"$vSlash$fileName\"}")
        }
        return "[${arr.joinToString(",")}]"
    }

    // Các lệnh Create/Write/Delete/Mkdir trả về thành công có thể sẽ là mảng rỗng (0 bytes).
    // => Chỉ cần check != null là ĐÃ THÀNH CÔNG!
    fun vfsMkdir(isQuic: Boolean, path: String) = sendRawKcp(0x05, resolvePath(path), 0, 0, null) != null
    fun vfsDelete(isQuic: Boolean, path: String) = sendRawKcp(0x06, resolvePath(path), 0, 0, null) != null
    fun vfsCreate(isQuic: Boolean, path: String) = sendRawKcp(0x04, resolvePath(path), 0, 0, ByteArray(0)) != null
    fun vfsWrite(isQuic: Boolean, path: String, offset: Long, data: ByteArray) = sendRawKcp(0x04, resolvePath(path), offset, 0, data) != null
    fun vfsRename(isQuic: Boolean, oldP: String, newP: String) = sendRawKcp(0x07, resolvePath(oldP), 0, 0, resolvePath(newP).toByteArray()) != null

    fun vfsDownload(isQuic: Boolean, remoteP: String, localP: String): Boolean {
        val statRes = sendRawKcp(0x01, resolvePath(remoteP), 0, 0, null) ?: return false
        if (statRes.size < 8) return false
        val size = ByteBuffer.wrap(statRes).order(ByteOrder.LITTLE_ENDIAN).long
        if (size == 0L) { File(localP).writeBytes(ByteArray(0)); return true }
        val raf = try { RandomAccessFile(localP, "rw").apply { setLength(size) } } catch (e: Exception) { return false }
        val chunkSize = 320 * 1024L // 320KB Max Limit
        val isSuccess = AtomicBoolean(true)
        val downloadedBytes = AtomicLong(0L)
        val executor = Executors.newFixedThreadPool(8)
        var offset = 0L
        while (offset < size) {
            val currentOffset = offset
            val reqLen = if (size - currentOffset < chunkSize) (size - currentOffset).toInt() else chunkSize.toInt()
            executor.submit {
                if (!isSuccess.get()) return@submit
                var retry = 0; var chunkSuccess = false
                while (retry < 3 && !chunkSuccess) {
                    val data = sendRawKcp(0x03, resolvePath(remoteP), currentOffset, reqLen, null)
                    // 🔥 ĐÃ FIX: Chỉ cần data trả về không rỗng là múc, không ép phải đúng y boong reqLen
                    if (data != null && data.isNotEmpty()) {
                        synchronized(raf) { raf.seek(currentOffset); raf.write(data) }
                        chunkSuccess = true
                        val totalNow = downloadedBytes.addAndGet(data.size.toLong())
                        if ((totalNow * 100 / size).toInt() / 10 > ((totalNow - data.size) * 100 / size).toInt() / 10 || totalNow >= size) {
                            RealtimeLogger.i("DOWNLOAD", "⬇️ Tải: ${(totalNow * 100 / size)}% ($totalNow/$size)")
                        }
                    } else { retry++; RealtimeLogger.e("DOWNLOAD", "⚠️ Lỗi block offset $currentOffset, retry$retry/3...") }
                }
                if (!chunkSuccess) isSuccess.set(false)
            }
            offset += reqLen
        }
        executor.shutdown(); try { executor.awaitTermination(1, TimeUnit.HOURS) } catch (e: Exception) { isSuccess.set(false) }
        try { raf.close() } catch (e: Exception) {}
        if (!isSuccess.get()) File(localP).delete()
        return isSuccess.get()
    }
}

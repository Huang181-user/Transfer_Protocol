package com.example.transfer_server

import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

object KcpNative {
    var remoteRoot: String = ""

    init {
        try {
            System.loadLibrary("zhiauth_kcp_jni")
            Log.i("HUANG_KCP_JNI", "Đã nạp thành công NDK libzhiauth_kcp_jni.so!")
        } catch (e: Exception) { Log.e("HUANG_KCP_JNI", "Lỗi nạp C++: ${e.message}") }
    }

    external fun initKcp(serverIp: String, port: Int, masterKey: String, mtu: Int): Boolean
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
                val parts = t.split(",")
                if (parts.size >= 2) {
                    val isDir = if (parts[1].trim() == "DIR") "true" else "false"
                    val fileName = parts[0].replace("\"", "").trim()
                    val fileSize = if (parts.size >= 3) parts[2].replace("\"", "").trim() else "0"
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
    
    // 🔥 THUẬT TOÁN TẢI SONG SONG (MULTI-PART) VẮT KIỆT BĂNG THÔNG
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

        // Cấp phát trước dung lượng ổ cứng (Chống phân mảnh)
        val raf = try {
            RandomAccessFile(localP, "rw").apply { setLength(size) }
        } catch (e: Exception) {
            Log.e("HUANG_KCP_JNI", "Lỗi tạo file RandomAccessFile: ${e.message}")
            return false
        }
        
        val chunkSize = 256 * 1024L // Băm mỗi mảnh 256KB
        val isSuccess = AtomicBoolean(true)
        val numThreads = 8 // Mở 8 nòng súng bắn song song
        val executor = Executors.newFixedThreadPool(numThreads)

        Log.i("HUANG_KCP_JNI", "🚀 Kích hoạt Tải Song Song: 8 Luồng | Kích thước: $size bytes")

        var offset = 0L
        while (offset < size) {
            val currentOffset = offset
            val reqLen = if (size - currentOffset < chunkSize) (size - currentOffset).toInt() else chunkSize.toInt()
            
            executor.submit {
                if (!isSuccess.get()) return@submit
                
                var retry = 0
                var chunkSuccess = false
                while (retry < 3 && !chunkSuccess) {
                    // Gọi qua C++ JNI (An toàn vì đã bọc Mutex ở C++)
                    val data = sendRawKcp(0x03, realPath, currentOffset, reqLen, null)
                    
                    if (data != null && data.size == reqLen) {
                        synchronized(raf) {
                            raf.seek(currentOffset)
                            raf.write(data)
                        }
                        chunkSuccess = true
                    } else {
                        retry++
                        Log.e("HUANG_KCP_JNI", "⚠️ Lỗi chunk offset $currentOffset, đang retry $retry/3...")
                    }
                }
                
                if (!chunkSuccess) {
                    Log.e("HUANG_KCP_JNI", "❌ Chunk offset $currentOffset thất bại hoàn toàn sau 3 lần thử!")
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
            File(localP).delete() // Dọn dẹp rác nếu tèo
            Log.e("HUANG_KCP_JNI", "❌ Tiến trình tải Multi-part thất bại: $localP")
        } else {
            Log.i("HUANG_KCP_JNI", "🎉 Tải song song HOÀN TẤT: $localP ($size bytes)")
        }
        
        return isSuccess.get()
    }
}

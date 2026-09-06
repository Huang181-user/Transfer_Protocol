package com.example.transfer_server
import java.io.InputStream
import com.example.app.network.RealtimeLogger

object HuangTransport {
    private const val TAG = "HUANG_TRANSPORT"
    fun getStat(isQuic: Boolean, path: String) = ZhiAuthNative.vfsStat(isQuic, path)
    fun listFiles(isQuic: Boolean, path: String) = ZhiAuthNative.vfsList(isQuic, path)
    fun createEmptyFile(isQuic: Boolean, path: String) = ZhiAuthNative.vfsCreate(isQuic, path)
    fun makeDirectory(isQuic: Boolean, path: String) = ZhiAuthNative.vfsMkdir(isQuic, path)
    fun deleteFile(isQuic: Boolean, path: String) = ZhiAuthNative.vfsDelete(isQuic, path)
    fun renameOrMoveFile(isQuic: Boolean, oldPath: String, newPath: String) = ZhiAuthNative.vfsRename(isQuic, oldPath, newPath)
    fun download(isQuic: Boolean, remotePath: String, localPath: String) = ZhiAuthNative.vfsDownload(isQuic, remotePath, localPath)

    fun uploadStream(isQuic: Boolean, path: String, inputStream: InputStream) {
        ZhiAuthNative.vfsCreate(isQuic, path)
        val buffer = ByteArray(256 * 1024)
        var offset: Long = 0
        var bytesRead: Int
        while (inputStream.read(buffer).also { bytesRead = it } != -1) {
            if (bytesRead > 0) {
                ZhiAuthNative.vfsWrite(isQuic, path, offset, buffer.copyOfRange(0, bytesRead))
                offset += bytesRead.toLong()
            }
        }
        RealtimeLogger.i(TAG, "🚀 Upload hoàn tất: $path (Total: $offset bytes)")
    }
}

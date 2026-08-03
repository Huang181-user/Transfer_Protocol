package com.example.transfer_server

import quicclient.Quicclient
import java.io.InputStream
import android.util.Log

object HuangTransport {
    private const val TAG = "HUANG_TRANSPORT"

    fun getStat(isQuic: Boolean, path: String): String = if(isQuic) Quicclient.vfsStat(path) else KcpNative.vfsStat(path)
    fun listFiles(isQuic: Boolean, path: String): String = if(isQuic) Quicclient.vfsList(path) else KcpNative.vfsList(path)
    fun createEmptyFile(isQuic: Boolean, path: String) = if(isQuic) Quicclient.vfsCreate(path) else KcpNative.vfsCreate(path)
    fun makeDirectory(isQuic: Boolean, path: String) = if(isQuic) Quicclient.vfsMkdir(path) else KcpNative.vfsMkdir(path)
    fun deleteFile(isQuic: Boolean, path: String) = if(isQuic) Quicclient.vfsDelete(path) else KcpNative.vfsDelete(path)
    fun renameOrMoveFile(isQuic: Boolean, oldPath: String, newPath: String) = if(isQuic) Quicclient.vfsRename(oldPath, newPath) else KcpNative.vfsRename(oldPath, newPath)

    fun download(isQuic: Boolean, remotePath: String, localPath: String): Boolean {
        return if(isQuic) Quicclient.vfsDownload(remotePath, localPath) else KcpNative.vfsDownload(remotePath, localPath)
    }

    fun uploadStream(isQuic: Boolean, path: String, inputStream: InputStream) {
        Log.d(TAG, "Bắt đầu upload luồng dữ liệu tới: $path (isQuic=$isQuic)")
        if(isQuic) Quicclient.vfsCreate(path) else KcpNative.vfsCreate(path)
        
        val buffer = ByteArray(256 * 1024)
        var offset: Long = 0
        var bytesRead: Int
        while (inputStream.read(buffer).also { bytesRead = it } != -1) {
            if (bytesRead > 0) {
                val chunk = buffer.copyOfRange(0, bytesRead)
                if(isQuic) Quicclient.vfsWrite(path, offset, chunk) else KcpNative.vfsWrite(path, offset, chunk)
                offset += bytesRead.toLong()
            }
        }
        Log.d(TAG, "Upload thành công mĩ mãn: $path ($offset bytes)")
    }
}

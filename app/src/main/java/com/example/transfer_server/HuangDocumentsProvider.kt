package com.example.transfer_server

import android.content.Context
import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsProvider
import android.util.Log
import android.webkit.MimeTypeMap
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileNotFoundException

class HuangDocumentsProvider : DocumentsProvider() {
    private val TAG = "HUANG_DOC_PROVIDER"
    private val AUTHORITY = "com.example.transfer_server.documents"

    private fun triggerUIRefresh(parentId: String) {
        val parentUri = DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId)
        context?.contentResolver?.notifyChange(parentUri, null)
    }

    private val COLUMNS = arrayOf(
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_FLAGS,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED
    )

    override fun onCreate(): Boolean {
        Log.d(TAG, "[INIT] Khởi tạo HuangDocumentsProvider...")
        context?.let { AppSecrets.init(it) }
        return true
    }

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: arrayOf(
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_FLAGS
        ))

        var currentUser = ""
        context?.let {
            val prefs = it.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
            currentUser = prefs.getString("user", "") ?: ""
        }
        val userSuffix = if (currentUser.isNotEmpty()) " ($currentUser)" else ""

        // 🟢 Ổ ĐĨA 1: QUIC VFS
        cursor.newRow().apply {
            add(DocumentsContract.Root.COLUMN_ROOT_ID, "${currentUser}_quic_root")
            add(DocumentsContract.Root.COLUMN_TITLE, "ZhiAuth QUIC$userSuffix")
            add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, "quic_/")
            add(DocumentsContract.Root.COLUMN_FLAGS, DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD or DocumentsContract.Root.FLAG_SUPPORTS_CREATE)
        }

        // 🔵 Ổ ĐĨA 2: KCP UDP (Trục truyền tải chính)
        cursor.newRow().apply {
            add(DocumentsContract.Root.COLUMN_ROOT_ID, "${currentUser}_kcp_root")
            add(DocumentsContract.Root.COLUMN_TITLE, "ZhiAuth KCP$userSuffix")
            add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, "kcp_/")
            add(DocumentsContract.Root.COLUMN_FLAGS, DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD or DocumentsContract.Root.FLAG_SUPPORTS_CREATE)
        }

        return cursor
    }

    private fun isInvalid(id: String?): Boolean {
        return id.isNullOrEmpty() || id.contains("document")
    }

    private fun extractPathInfo(docId: String): Pair<Boolean, String> {
        val isQuic = docId.startsWith("quic_")
        var realPath = if (isQuic) docId.removePrefix("quic_") else docId.removePrefix("kcp_")

        if (realPath.isEmpty() || realPath == "/") {
            realPath = "/"
        } else if (!realPath.startsWith("/")) {
            realPath = "/$realPath"
        }
        return Pair(isQuic, realPath)
    }

    override fun isChildDocument(parentDocumentId: String?, documentId: String?): Boolean {
        val parent = parentDocumentId ?: return false
        val child = documentId ?: return false
        val parentPath = if (parent.endsWith("/")) parent else "$parent/"
        return child.startsWith(parentPath) && child != parent
    }

    override fun queryDocument(documentId: String?, projection: Array<out String>?): Cursor {
        if (isInvalid(documentId)) return MatrixCursor(projection ?: COLUMNS)
        val cursor = MatrixCursor(projection ?: COLUMNS)
        val safeDocId = documentId ?: "quic_/"

        val (isQuic, realPath) = extractPathInfo(safeDocId)

        val json = HuangTransport.getStat(isQuic, realPath)

        if (!json.trim().startsWith("{")) return cursor

        try {
            val obj = JSONObject(json)
            if (!obj.has("is_dir")) {
                Log.w(TAG, "[QUERY_DOC] JSON lỗi hoặc không có quyền: $json")
                return cursor
            }

            val isDir = obj.getBoolean("is_dir")

            // Hứng mtime từ JSON (Ctime, Atime, Mode hiện tại Android SAF không có cột chuẩn để hiện)
            val mtime = if (obj.has("last_modified")) obj.getLong("last_modified") else 0L

            var flags = DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                    DocumentsContract.Document.FLAG_SUPPORTS_RENAME or
                    DocumentsContract.Document.FLAG_SUPPORTS_MOVE or
                    DocumentsContract.Document.FLAG_SUPPORTS_COPY or
                    DocumentsContract.Document.FLAG_SUPPORTS_WRITE

            if (isDir) flags = flags or DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE

            val name = obj.getString("name")
            val size = obj.getLong("size")
            val mime = if(isDir) DocumentsContract.Document.MIME_TYPE_DIR else getMimeType(name)

            cursor.newRow().apply {
                add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, safeDocId)
                add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, name)
                add(DocumentsContract.Document.COLUMN_MIME_TYPE, mime)
                add(DocumentsContract.Document.COLUMN_SIZE, size)
                add(DocumentsContract.Document.COLUMN_FLAGS, flags)

                // 🔥 Đẩy thời gian sửa đổi (mtime) vào giao diện Android
                if (mtime > 0L) {
                    add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, mtime)
                }
            }
        } catch (e: Exception) { Log.e(TAG, "[QUERY_DOC] Lỗi parse JSON: ${e.message} | Payload: $json") }
        return cursor
    }

    override fun queryChildDocuments(parentDocumentId: String?, projection: Array<out String>?, sortOrder: String?): Cursor {
        if (isInvalid(parentDocumentId)) return MatrixCursor(projection ?: COLUMNS)
        val matrix = MatrixCursor(projection ?: COLUMNS)
        val safeParentId = parentDocumentId ?: "quic_/"

        val (isQuic, realPath) = extractPathInfo(safeParentId)
        
        // Phân luồng rạch ròi: QUIC đi đường QUIC, KCP đi đường KCP
        val json = HuangTransport.listFiles(isQuic, realPath)

        if (!json.trim().startsWith("[")) return matrix

        try {
            val array = JSONArray(json)
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                if (!obj.has("is_dir")) continue

                val isDir = obj.getBoolean("is_dir")
                var flags = DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                        DocumentsContract.Document.FLAG_SUPPORTS_RENAME or
                        DocumentsContract.Document.FLAG_SUPPORTS_MOVE or
                        DocumentsContract.Document.FLAG_SUPPORTS_COPY or
                        DocumentsContract.Document.FLAG_SUPPORTS_WRITE

                if (isDir) flags = flags or DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE

                val rawChildPath = obj.getString("path")
                val childId = if (isQuic) "quic_$rawChildPath" else "kcp_$rawChildPath"
                val name = obj.getString("name")
                val size = obj.getLong("size")
                val mime = if(isDir) DocumentsContract.Document.MIME_TYPE_DIR else getMimeType(name)

                matrix.newRow().apply {
                    add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, childId)
                    add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, name)
                    add(DocumentsContract.Document.COLUMN_SIZE, size)
                    add(DocumentsContract.Document.COLUMN_MIME_TYPE, mime)
                    add(DocumentsContract.Document.COLUMN_FLAGS, flags)
                }
            }
        } catch (e: Exception) { Log.e(TAG, "[QUERY_CHILD] Lỗi phân tích JSON mảng: ${e.message} | Payload: $json") }
        return matrix
    }

    override fun createDocument(parentDocumentId: String?, mimeType: String?, displayName: String?): String {
        val safeParent = parentDocumentId ?: "quic_/"
        val safeName = displayName ?: "NewFile"

        val (isQuic, realParentPath) = extractPathInfo(safeParent)
        val newRealPath = if (realParentPath == "/") "/$safeName" else "$realParentPath/$safeName"
        val newId = if (isQuic) "quic_$newRealPath" else "kcp_$newRealPath"

        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
            HuangTransport.makeDirectory(isQuic, newRealPath)
        } else {
            HuangTransport.createEmptyFile(isQuic, newRealPath)
        }

        triggerUIRefresh(safeParent)
        return newId
    }

    override fun deleteDocument(documentId: String?) {
        val safeDocId = documentId ?: return
        val (isQuic, realPath) = extractPathInfo(safeDocId)

        val parentIdUI = if (realPath.lastIndexOf('/') > 0) {
            if (isQuic) "quic_${realPath.substringBeforeLast("/")}" else "kcp_${realPath.substringBeforeLast("/")}"
        } else {
            if (isQuic) "quic_/" else "kcp_/"
        }

        HuangTransport.deleteFile(isQuic, realPath)
        triggerUIRefresh(parentIdUI)
    }

    override fun removeDocument(documentId: String?, parentDocumentId: String?) {
        deleteDocument(documentId)
    }

    override fun renameDocument(documentId: String?, displayName: String?): String {
        val safeDocId = documentId ?: return ""
        val safeName = displayName ?: return safeDocId

        val (isQuic, realPath) = extractPathInfo(safeDocId)

        val realParentPath = if (realPath.lastIndexOf('/') > 0) realPath.substringBeforeLast("/") else "/"
        val newRealPath = if (realParentPath == "/") "/$safeName" else "$realParentPath/$safeName"

        HuangTransport.renameOrMoveFile(isQuic, realPath, newRealPath)

        val parentIdUI = if (realParentPath == "/") {
            if (isQuic) "quic_/" else "kcp_/"
        } else {
            if (isQuic) "quic_$realParentPath" else "kcp_$realParentPath"
        }

        val newIdUI = if (isQuic) "quic_$newRealPath" else "kcp_$newRealPath"
        triggerUIRefresh(parentIdUI)
        return newIdUI
    }

    override fun moveDocument(sourceDocumentId: String?, sourceParentDocumentId: String?, targetParentDocumentId: String?): String {
        val safeSourceId = sourceDocumentId ?: return ""
        val safeTargetParentId = targetParentDocumentId ?: return ""
        val safeSourceParentId = sourceParentDocumentId ?: return ""

        val (isQuic, realSourcePath) = extractPathInfo(safeSourceId)
        val (_, realTargetParentPath) = extractPathInfo(safeTargetParentId)

        val name = realSourcePath.substringAfterLast("/")
        val newRealPath = if (realTargetParentPath == "/") "/$name" else "$realTargetParentPath/$name"

        HuangTransport.renameOrMoveFile(isQuic, realSourcePath, newRealPath)

        val newIdUI = if (isQuic) "quic_$newRealPath" else "kcp_$newRealPath"

        triggerUIRefresh(safeSourceParentId)
        triggerUIRefresh(safeTargetParentId)
        return newIdUI
    }

    override fun openDocument(documentId: String?, mode: String?, signal: CancellationSignal?): ParcelFileDescriptor {
        val safeDocId = documentId ?: throw FileNotFoundException("ID null")
        val (isQuic, realPath) = extractPathInfo(safeDocId)

        if (mode == "r") {
            val file = File(context?.cacheDir, safeDocId.substringAfterLast("/"))
            val success = HuangTransport.download(isQuic, realPath, file.absolutePath)

            if (success) {
                return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
            }
            throw FileNotFoundException("Lỗi tải file từ Server!")
        } else {
            val pipe = ParcelFileDescriptor.createReliablePipe()
            Thread {
                try {
                    ParcelFileDescriptor.AutoCloseInputStream(pipe[0]).use {
                        HuangTransport.uploadStream(isQuic, realPath, it)
                    }
                } catch (e: Exception) {
                    pipe[0].closeWithError(e.message)
                }
            }.start()
            return pipe[1]
        }
    }

    private fun getMimeType(name: String): String {
        val ext = name.substringAfterLast(".", "")
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }
}

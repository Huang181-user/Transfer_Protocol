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
import org.json.JSONObject
import org.json.JSONArray
import java.io.File
import java.io.FileNotFoundException

class HuangDocumentsProvider : DocumentsProvider() {
    private val TAG = "HUANG_DOC_PROVIDER"

    private val COLUMNS = arrayOf(
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_FLAGS
    )

    override fun onCreate(): Boolean {
        Log.d(TAG, "[PROVIDER_ON_CREATE] 🛠️ Khởi tạo DocumentsProvider hệ thống.")
        context?.let {
            AppSecrets.init(it)
            val prefs = it.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
            NetworkConfig.QUIC_USER = prefs.getString("user", AppSecrets.SFTP_USER) ?: AppSecrets.SFTP_USER
            NetworkConfig.QUIC_PASS = prefs.getString("pass", AppSecrets.SFTP_PASS) ?: AppSecrets.SFTP_PASS

            // 🎯 Đồng bộ ép cứng cấu hình đường dẫn tương đối
            NetworkConfig.ROOT_PATH = "/"
            NetworkConfig.SERVER_IP = prefs.getString("server_ip", "") ?: AppSecrets.LOCAL_IP
            NetworkConfig.QUIC_PORT = AppSecrets.QUIC_PORT
            NetworkConfig.SNI_DOMAIN = AppSecrets.SNI_DOMAIN
        }
        return true
    }

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: arrayOf(
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_FLAGS
        ))

        // 🎯 Đọc tươi dữ liệu tài khoản mới nhất từ két sắt SharedPreferences mỗi khi SAF quét ổ đĩa
        context?.let {
            val prefs = it.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
            NetworkConfig.QUIC_USER = prefs.getString("user", AppSecrets.SFTP_USER) ?: AppSecrets.SFTP_USER
            NetworkConfig.QUIC_PASS = prefs.getString("pass", AppSecrets.SFTP_PASS) ?: AppSecrets.SFTP_PASS
        }

        val user = NetworkConfig.QUIC_USER

        // 🎯 ÉP CỨNG TUYỆT ĐỐI ĐƯỜNG DẪN ẢO LÀ "/" CHO MỌI USER ĐĂNG NHẬP
        val rootPath = "/"

        Log.d(TAG, "==========================================================================")
        Log.d(TAG, "[QUERY_ROOTS] 📂 Hệ thống Android yêu cầu quét danh mục Ổ Đĩa Gốc (Roots)")
        Log.d(TAG, " -> Đăng nhập bằng danh tính User : $user")
        Log.d(TAG, " -> Áp dụng cấu hình ảo RootPath : $rootPath")

        cursor.newRow().apply {
            // Định danh Root ID độc nhất động theo tên user để ép Android xóa sạch cache cũ
            val uniqueRootId = "${user}_virtual_root"
            add(DocumentsContract.Root.COLUMN_ROOT_ID, uniqueRootId)
            add(DocumentsContract.Root.COLUMN_TITLE, "Huang Server ($user)")
            add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, rootPath) // Luôn truyền "/" lên hệ thống
            add(DocumentsContract.Root.COLUMN_FLAGS, DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD or DocumentsContract.Root.FLAG_SUPPORTS_CREATE)
        }
        Log.d(TAG, "==========================================================================")
        return cursor
    }

    private fun isInvalid(id: String?): Boolean {
        return id.isNullOrEmpty() || id.contains("document") || NetworkConfig.SERVER_IP.isEmpty()
    }

    override fun queryDocument(documentId: String?, projection: Array<out String>?): Cursor {
        if (isInvalid(documentId)) return MatrixCursor(projection ?: COLUMNS)
        val cursor = MatrixCursor(projection ?: COLUMNS)

        Log.d(TAG, "[QUERY_DOCUMENT] 🔍 Android xin thông tin thực thể (Stat) của id: '$documentId'")

        val json = HuangTransport.getStat(documentId!!)

        if (!json.trim().startsWith("{")) {
            Log.e(TAG, " -> ❌ LỖI: Server trả dữ liệu rác hoặc từ chối kết nối (Chuỗi trả về: '$json')")
            return cursor
        }

        try {
            val obj = JSONObject(json)
            val isDir = obj.getBoolean("is_dir")
            var flags = DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                    DocumentsContract.Document.FLAG_SUPPORTS_RENAME or
                    DocumentsContract.Document.FLAG_SUPPORTS_MOVE or
                    DocumentsContract.Document.FLAG_SUPPORTS_WRITE
            if (isDir) flags = flags or DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE

            cursor.newRow().apply {
                add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, documentId)
                add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, obj.getString("name"))
                add(DocumentsContract.Document.COLUMN_MIME_TYPE, if(isDir) DocumentsContract.Document.MIME_TYPE_DIR else getMimeType(obj.getString("name")))
                add(DocumentsContract.Document.COLUMN_SIZE, obj.getLong("size"))
                add(DocumentsContract.Document.COLUMN_FLAGS, flags)
            }
            Log.d(TAG, " -> ✅ THÀNH CÔNG: Đã phân tích Stat [Tên: ${obj.getString("name")} | Thư mục: $isDir]")
        } catch (e: Exception) {
            Log.e(TAG, " -> 💥 CRASH PARSE JSON STAT: ${e.message}")
        }
        return cursor
    }

    override fun queryChildDocuments(parentDocId: String?, projection: Array<out String>?, sortOrder: String?): Cursor {
        if (isInvalid(parentDocId)) return MatrixCursor(projection ?: COLUMNS)
        val matrix = MatrixCursor(projection ?: COLUMNS)

        Log.d(TAG, "[QUERY_CHILDREN] 📁 Android yêu cầu quét danh sách file con bên trong thư mục: '$parentDocId'")

        val json = HuangTransport.listFiles(parentDocId!!)

        if (!json.trim().startsWith("[")) {
            Log.e(TAG, " -> ❌ LỖI: Cấu trúc mảng JSON bị gãy hoặc Server chặn đường dẫn (Chuỗi: '$json')")
            return matrix
        }

        try {
            val array = JSONArray(json)
            Log.d(TAG, " -> Nạp thành công mảng gồm ${array.length()} đối tượng từ server Go. Đang đổ vào UI...")
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                val isDir = obj.getBoolean("is_dir")
                var flags = DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                        DocumentsContract.Document.FLAG_SUPPORTS_RENAME or
                        DocumentsContract.Document.FLAG_SUPPORTS_MOVE or
                        DocumentsContract.Document.FLAG_SUPPORTS_WRITE
                if (isDir) flags = flags or DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE

                matrix.newRow().apply {
                    add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, obj.getString("path"))
                    add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, obj.getString("name"))
                    add(DocumentsContract.Document.COLUMN_SIZE, obj.getLong("size"))
                    add(DocumentsContract.Document.COLUMN_MIME_TYPE, if(isDir) DocumentsContract.Document.MIME_TYPE_DIR else getMimeType(obj.getString("name")))
                    add(DocumentsContract.Document.COLUMN_FLAGS, flags)
                }
            }
            Log.d(TAG, " -> ✅ Đã đồng bộ mảng dữ liệu con lên File Explorer hoàn tất.")
        } catch (e: Exception) {
            Log.e(TAG, " -> 💥 CRASH PARSE JSON ARRAY CHILDREN: ${e.message}")
        }
        return matrix
    }

    override fun openDocument(documentId: String?, mode: String?, signal: CancellationSignal?): ParcelFileDescriptor {
        Log.d(TAG, "[OPEN_DOCUMENT] 📥 Người dùng bấm tải xuống hoặc xem trực tiếp file: '$documentId'")
        val file = File(context?.cacheDir, documentId?.substringAfterLast("/") ?: "tmp")
        if (HuangTransport.download(documentId!!, file.absolutePath)) {
            Log.d(TAG, " -> ✅ TẢI FILE THÀNH CÔNG VỀ BỘ NHỚ ĐỆM: ${file.absolutePath}")
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
        }
        Log.e(TAG, " -> ❌ THẤT BẠI: Không thể kéo luồng I/O từ Server cho file: '$documentId'")
        throw FileNotFoundException()
    }

    override fun createDocument(parentDocumentId: String?, mimeType: String?, displayName: String?): String {
        Log.d(TAG, "[CREATE_DOCUMENT] Lệnh tạo thực thể mới ảo nhận diện: $displayName tại $parentDocumentId")
        return "$parentDocumentId/$displayName"
    }

    override fun renameDocument(documentId: String?, displayName: String?): String {
        Log.d(TAG, "[RENAME_DOCUMENT] Yêu cầu đổi tên nhận diện: $documentId thành $displayName")
        return documentId ?: ""
    }

    override fun deleteDocument(documentId: String?) {
        Log.d(TAG, "[DELETE_DOCUMENT] Yêu cầu xóa nhận diện thực thể: $documentId")
    }

    override fun moveDocument(sourceDocumentId: String?, sourceParentDocumentId: String?, targetParentDocumentId: String?): String {
        Log.d(TAG, "[MOVE_DOCUMENT] Yêu cầu Di chuyển (Cut) từ $sourceDocumentId sang $targetParentDocumentId")
        return sourceDocumentId ?: ""
    }

    private fun getMimeType(name: String): String {
        val ext = name.substringAfterLast(".", "")
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }
}
package com.example.transfer_server

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import quicclient.Quicclient

object QuicTransfer {
    private const val TAG = "QUIC_DEBUG_CORE"

    suspend fun downloadFast(url: String, savePath: String, customMtu: Int): Boolean = withContext(Dispatchers.IO) {
        val safeUrl = url.replace(Regex("://.*:.*@"), "://***:***@")

        Log.d(TAG, "==========================================================================")
        Log.d(TAG, "[QUIC_START] KÍCH HOẠT LÕI GOMOBILE - GIAO THỨC HTTP/3 QUIC")
        Log.d(TAG, "[QUIC_INFO] URL Nguồn   : $safeUrl") // In chuỗi đã che
        Log.d(TAG, "[QUIC_INFO] Đích Lưu    : $savePath")
        Log.d(TAG, "[QUIC_INFO] MTU Ép Dòng : $customMtu bytes")
        Log.d(TAG, "==========================================================================")

        try {
            Log.d(TAG, "[QUIC_EXECUTE] Đang đẩy lệnh xuống tầng C/C++ của Go...")

            // 🎯 Đã thêm tham số NetworkConfig.SNI_DOMAIN vào đây
            val result = Quicclient.downloadFast(url, savePath, customMtu.toLong(), NetworkConfig.SNI_DOMAIN)

            Log.d(TAG, "[QUIC_RESPONSE] Dữ liệu thô từ Go trả về: [$result]")

            // Lõi Go trả về "SUCCESS..."
            if (result.startsWith("SUCCESS") || result.startsWith("QUIC_SUCCESS")) {
                Log.d(TAG, "[QUIC_SUCCESS] 🚀 BOOM! TẢI XONG! TỐC ĐỘ BÀN THỜ LÀ ĐÂY!")
                Log.d(TAG, "==========================================================================")
                return@withContext true
            } else {
                Log.e(TAG, "[QUIC_ERROR] ❌ LỖI TỪ LÕI GO: $result")
                Log.d(TAG, "==========================================================================")
                return@withContext false
            }
        } catch (e: Exception) {
            Log.e(TAG, "[QUIC_FATAL] 💥 APP CRASH KHI GIAO TIẾP VỚI LÕI GO: ${e.message}")
            Log.d(TAG, "==========================================================================")
            return@withContext false
        }
    }
}
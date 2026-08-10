package com.example.app.network

import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object RealtimeLogger {
    private const val DEFAULT_TAG = "ABM_Network"
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())

    private fun getTimestamp(): String {
        return dateFormat.format(Date())
    }

    // 🔥 BỘ LỌC CHE GIẤU DỮ LIỆU NHẠY CẢM (MASKING) TRƯỚC KHI IN RA LOGCAT
    private fun maskSensitiveData(msg: String): String {
        var safeMsg = msg

        // 1. Che IP LAN & Tailscale (VD: 100.125.141.48 -> 100.***.***.48)
        val ipRegex = Regex("\\b(\\d{1,3})\\.\\d{1,3}\\.\\d{1,3}\\.(\\d{1,3})\\b")
        safeMsg = ipRegex.replace(safeMsg, "$1.***.***.$2")

        // 2. Che đường dẫn tệp tin nhưng giữ lại tên file để dễ Debug
        // VD: /mnt/HDD_merge/Huang_Datas/Works/LVDH_TY-2022.docx -> /***/***/LVDH_TY-2022.docx
        val pathRegex = Regex("(/[^\\s\"',:]+)+/([^\\s\"',:]+)")
        safeMsg = pathRegex.replace(safeMsg) { match ->
            val fileName = match.groupValues[2]
            "/***/***/$fileName"
        }

        // 3. Che các trường nhạy cảm trong chuỗi JSON (nếu có in ra)
        val jsonPathRegex = Regex("\"path\"\\s*:\\s*\"([^\"]+)/([^\"]+)\"")
        safeMsg = jsonPathRegex.replace(safeMsg) { match ->
            val fileName = match.groupValues[2]
            "\"path\":\"/***/***/$fileName\""
        }

        return safeMsg
    }

    fun d(tag: String, message: String) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [DEBUG] [$tag] $safeMessage"
        Log.d(DEFAULT_TAG, formattedMessage)
        println(formattedMessage) // Vẫn giữ in ra terminal cho dễ nhìn
    }

    fun i(tag: String, message: String) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [INFO] [$tag] $safeMessage"
        Log.i(DEFAULT_TAG, formattedMessage)
        println(formattedMessage)
    }

    fun e(tag: String, message: String, throwable: Throwable? = null) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [ERROR] [$tag] $safeMessage"
        Log.e(DEFAULT_TAG, formattedMessage, throwable)
        println(formattedMessage)
        throwable?.printStackTrace()
    }
}
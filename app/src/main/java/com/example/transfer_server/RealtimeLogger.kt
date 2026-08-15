package com.example.app.network

import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object RealtimeLogger {
    private const val DEFAULT_TAG = "ABM_Network"
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())

    // 🔥 TẠO LUỒNG LƯU LOG ĐỂ BẮN THẲNG LÊN UI TRONG APP
    private val _appLogs = MutableStateFlow("Khởi động hệ thống ZhiAuth Logger...\n")
    val appLogs: StateFlow<String> = _appLogs

    private fun getTimestamp(): String {
        return dateFormat.format(Date())
    }

    private fun maskSensitiveData(msg: String): String {
        var safeMsg = msg
        val ipRegex = Regex("\\b(\\d{1,3})\\.\\d{1,3}\\.\\d{1,3}\\.(\\d{1,3})\\b")
        safeMsg = ipRegex.replace(safeMsg, "$1.***.***.$2")

        val pathRegex = Regex("(/[^\\s\"',:]+)+/([^\\s\"',:]+)")
        safeMsg = pathRegex.replace(safeMsg) { match ->
            val fileName = match.groupValues[2]
            "/***/***/$fileName"
        }

        val jsonPathRegex = Regex("\"path\"\\s*:\\s*\"([^\"]+)/([^\"]+)\"")
        safeMsg = jsonPathRegex.replace(safeMsg) { match ->
            val fileName = match.groupValues[2]
            "\"path\":\"/***/***/$fileName\""
        }

        return safeMsg
    }

    // 🔥 HÀM ĐẨY LOG LÊN MÀN HÌNH CHÍNH CỦA APP
    private fun appendToAppUI(formattedMessage: String) {
        _appLogs.update { currentLogs ->
            val newLogs = "$formattedMessage\n$currentLogs"
            // Giới hạn 10,000 ký tự để không làm tràn RAM khi tải file lớn
            if (newLogs.length > 10000) newLogs.substring(0, 10000) else newLogs
        }
    }

    fun d(tag: String, message: String) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [DEBUG] [$tag] $safeMessage"
        Log.d(DEFAULT_TAG, formattedMessage)
        println(formattedMessage)
        appendToAppUI(formattedMessage)
    }

    fun i(tag: String, message: String) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [INFO] [$tag] $safeMessage"
        Log.i(DEFAULT_TAG, formattedMessage)
        println(formattedMessage)
        appendToAppUI(formattedMessage)
    }

    fun e(tag: String, message: String, throwable: Throwable? = null) {
        val safeMessage = maskSensitiveData(message)
        val formattedMessage = "[${getTimestamp()}] [ERROR] [$tag] $safeMessage"
        Log.e(DEFAULT_TAG, formattedMessage, throwable)
        println(formattedMessage)
        throwable?.printStackTrace()
        appendToAppUI(formattedMessage)
    }
}
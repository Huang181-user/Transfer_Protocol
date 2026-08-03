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

    fun d(tag: String, message: String) {
        val formattedMessage = "[${getTimestamp()}] [DEBUG] [$tag] $message"
        Log.d(DEFAULT_TAG, formattedMessage)
        println(formattedMessage)
    }

    fun i(tag: String, message: String) {
        val formattedMessage = "[${getTimestamp()}] [INFO] [$tag] $message"
        Log.i(DEFAULT_TAG, formattedMessage)
        println(formattedMessage)
    }

    fun e(tag: String, message: String, throwable: Throwable? = null) {
        val formattedMessage = "[${getTimestamp()}] [ERROR] [$tag] $message"
        Log.e(DEFAULT_TAG, formattedMessage, throwable)
        println(formattedMessage)
        throwable?.printStackTrace()
    }
}

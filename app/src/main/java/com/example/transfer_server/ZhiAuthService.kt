package com.example.transfer_server

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import quicclient.Quicclient
import android.util.Log

class ZhiAuthService : Service() {
    private val TAG = "ZHI_SERVICE"
    private var wakeLock: PowerManager.WakeLock? = null
    private lateinit var connectivityManager: ConnectivityManager
    private lateinit var networkCallback: ConnectivityManager.NetworkCallback

    override fun onCreate() {
        super.onCreate()
        
        val channelId = "ZhiAuth_Channel"
        val channel = NotificationChannel(channelId, "ZhiAuth Background", NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        
        val notification = NotificationCompat.Builder(this, channelId)
            .setContentTitle("ZhiAuth V6.0")
            .setContentText("Trục kép KCP & QUIC đang bảo vệ luồng dữ liệu...")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        startForeground(1999, notification)

        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "ZhiAuth::KcpWakeLock")
        wakeLock?.acquire()

        connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                Log.d(TAG, "Phát hiện có sóng mạng! Báo cho GoMobile Roaming...")
                KcpNative.reconnectSocket()
                Quicclient.triggerNetworkRoaming()
            }
            override fun onLost(network: Network) {
                Log.d(TAG, "Mất sóng mạng! Báo cho GoMobile Roaming...")
                Quicclient.triggerNetworkRoaming()
            }
        }
        connectivityManager.registerDefaultNetworkCallback(networkCallback)
    }

    override fun onDestroy() {
        wakeLock?.takeIf { it.isHeld }?.release()
        connectivityManager.unregisterNetworkCallback(networkCallback)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null
}

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
import android.util.Log

class ZhiAuthService : Service() {
    private val TAG = "ZHI_SERVICE"
    private var wakeLock: PowerManager.WakeLock? = null
    private lateinit var connectivityManager: ConnectivityManager
    private lateinit var networkCallback: ConnectivityManager.NetworkCallback
    private var isInitialCallback = true // 🔥 Cờ chặn kích nổ ảo lần đầu

    override fun onCreate() {
        super.onCreate()
        val channelId = "ZhiAuth_Channel"
        val channel = NotificationChannel(channelId, "ZhiAuth Background", NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        
        val notification = NotificationCompat.Builder(this, channelId)
            .setContentTitle("ZhiAuth V6.0")
            .setContentText("Trục KCP C++ đang bảo vệ luồng dữ liệu...")
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
                if (isInitialCallback) {
                    isInitialCallback = false
                    return
                }
                Log.d(TAG, "Phát hiện có sóng mạng hoặc đổi Wi-Fi! Đang Re-Auth UFW...")

                Thread {
                    // 🔥 CHỜ 2 GIÂY ĐỂ OS ANDROID CẬP NHẬT XONG BẢNG ĐỊNH TUYẾN ARP
                    Thread.sleep(2000)

                    // Gọi hàm Ping thông minh vừa sửa
                    NetworkConfig.SERVER_IP = NetworkConfig.discoverBestRouteSync()

                    val realLan = ZhiAuthNative.getDeviceIps().split("|").getOrNull(0) ?: "NONE"
                    val realTs = ZhiAuthNative.getDeviceIps().split("|").getOrNull(1) ?: "NONE"
                    val authPayload = "AUTH_REQ|USER:${NetworkConfig.QUIC_USER}|PASS:${NetworkConfig.QUIC_PASS}|LAN:$realLan|TS:$realTs|HWID:${NetworkConfig.HW_ID}"

                    // Reset cỗ máy QUIC và gõ cửa lại
                    ZhiAuthNative.shutdownQuic()
                    ZhiAuthNative.initMsQuic(NetworkConfig.SERVER_IP, NetworkConfig.AUTH_PORT.toInt(), NetworkConfig.QUIC_PORT.toInt())

                    val result = ZhiAuthNative.authMsQuic(authPayload)
                    if (result.startsWith("AUTH_SUCCESS")) {
                        Log.d(TAG, "Đã cập nhật IP mới vào Tường lửa Server. Đang nối lại Socket KCP...")
                        ZhiAuthNative.reconnectSocket()
                    }
                }.start()
            }
            override fun onLost(network: Network) {
                Log.d(TAG, "Mất sóng mạng! Chờ C++ Core xử lý...")
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

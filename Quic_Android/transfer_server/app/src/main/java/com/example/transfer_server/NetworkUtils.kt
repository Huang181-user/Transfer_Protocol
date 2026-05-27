package com.example.transfer_server

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import quicclient.Quicclient
import java.net.InetAddress

object NetworkUtils {
    private const val TAG = "HUANG_NET_UTILS"

    suspend fun pingHost(ip: String): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            // 🎯 Bịt luôn IP trên log Ping
            Log.d(TAG, "[PING] 🔍 Chọc thử TCP vào ***.***.***.***:** để test LAN...")
            val socket = java.net.Socket()
            socket.connect(java.net.InetSocketAddress(ip, 22), 1500)
            socket.close()
            Log.d(TAG, "[PING] ✅ LAN THÔNG SUỐT TRÊN HỆ THỐNG MẠNG!")
            true
        } catch (e: Exception) {
            Log.e(TAG, "[PING] ❌ LAN TỊT HOÀN TOÀN: Lỗi kết nối (IP đã ẩn)") // Che luôn chi tiết lỗi gốc nếu có chứa IP
            false
        }
    }

    suspend fun discoverBestMtu(targetUrl: String, isVpn: Boolean): Int = withContext(Dispatchers.IO) {
        var min = 1200
        var max = 1500
        var best = 1200
        Log.d(TAG, "==========================================================================")
        Log.d(TAG, "[MTU_SCAN] 🚀 BẮT ĐẦU CHẶT NHỊ PHÂN ĐO MTU VỚI MẶT NẠ SNI: đố bạn đoán được :)))")
        Log.d(TAG, "[MTU_SCAN] Mức chặn dưới: $min | Mức chặn trên: $max")
        Log.d(TAG, "==========================================================================")

        while (min <= max) {
            val mid = (min + max) / 2
            // 🎯 Đã truyền đầy đủ targetUrl, kích thước mid và mặt nạ SNI_DOMAIN xuống lõi Go mới
            if (Quicclient.probeMTU(targetUrl, mid.toLong(), NetworkConfig.SNI_DOMAIN)) {
                Log.d(TAG, "[MTU_SCAN] ✅ Gói tin $mid bytes: THÔNG SUỐT (Chấp nhận cấu hình)")
                best = mid
                min = mid + 1
            } else {
                Log.e(TAG, "[MTU_SCAN] ❌ Gói tin $mid bytes: NGHẼN HOẶC BỊ ĐẨY VÀO HẦM VPN")
                max = mid - 1
            }
        }
        Log.d(TAG, "==========================================================================")
        Log.d(TAG, "[MTU_SCAN] 🏆 KẾT LUẬN CHI TIẾT - MTU TỐT NHẤT PHÙ HỢP ĐƯỜNG TRUYỀN LÀ: $best")
        Log.d(TAG, "==========================================================================")
        return@withContext best
    }

    fun bindProcessToVpn(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        cm.allNetworks.forEach { network ->
            if (cm.getNetworkCapabilities(network)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true) {
                Log.d(TAG, "[VPN] 🔗 Đã thực hiện ép toàn bộ tiến trình App vào hầm Tailscale")
                return cm.bindProcessToNetwork(network)
            }
        }
        return false
    }

    fun clearProcessBinding(context: Context) {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        cm.bindProcessToNetwork(null)
        Log.d(TAG, "[VPN] 🔓 Đã gỡ bỏ toàn bộ ràng buộc mạng, trả kết nối về WiFi mặc định")
    }
}
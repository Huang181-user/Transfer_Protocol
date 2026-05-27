package com.example.transfer_server

import android.content.Context
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import com.example.transfer_server.ui.theme.Transfer_serverTheme

class MainActivity : ComponentActivity() {
    private val TAG = "HUANG_MAIN_ACTIVITY"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.d(TAG, "==========================================================================")
        Log.d(TAG, "[SYSTEM_START] 🚀 KHỞI CHẠY ỨNG DỤNG HUANG TRANSFER SERVER CLIENT")
        Log.d(TAG, "==========================================================================")

        AppSecrets.init(this)
        NetworkConfig.LOCAL_IP = AppSecrets.LOCAL_IP
        NetworkConfig.TS_IP = AppSecrets.TS_IP
        NetworkConfig.QUIC_PORT = AppSecrets.QUIC_PORT
        NetworkConfig.SNI_DOMAIN = AppSecrets.SNI_DOMAIN

        // Đọc lại tài khoản cũ đã lưu từ két sắt SharedPreferences
        val prefs = getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
        NetworkConfig.QUIC_USER = prefs.getString("user", AppSecrets.SFTP_USER) ?: AppSecrets.SFTP_USER
        NetworkConfig.QUIC_PASS = prefs.getString("pass", AppSecrets.SFTP_PASS) ?: AppSecrets.SFTP_PASS

        // Mặc định luôn là ROOT ảo "/"
        NetworkConfig.ROOT_PATH = "/"

        Log.d(TAG, "[PREFS_LOAD] Nạp cấu hình thành công từ bộ nhớ đệm:")
        Log.d(TAG, " -> User hiện tại : ${NetworkConfig.QUIC_USER}")
        Log.d(TAG, " -> Virtual Root  : ${NetworkConfig.ROOT_PATH}")
        Log.d(TAG, "==========================================================================")

        setContent {
            Transfer_serverTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    QuicSuperScreen()
                }
            }
        }
    }
}

@Composable
fun QuicSuperScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var logMsg by remember { mutableStateOf("Chưa đăng nhập...") }
    val TAG_UI = "HUANG_UI_CORE"

    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("HUANG QUIC LOGIN", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(10.dp))

        OutlinedTextField(NetworkConfig.LOCAL_IP, { NetworkConfig.LOCAL_IP = it }, label = { Text("IP LAN") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.TS_IP, { NetworkConfig.TS_IP = it }, label = { Text("IP Tailscale") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_USER, { NetworkConfig.QUIC_USER = it }, label = { Text("Tài khoản") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_PASS, { NetworkConfig.QUIC_PASS = it }, label = { Text("Mật khẩu") }, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())

        // 🎯 Ô NHẬP ROOT PATH ĐÃ ĐƯỢC XÓA BỎ HOÀN TOÀN TẠI ĐÂY GIÚP GIAO DIỆN SIÊU TINH GỌN

        Button(onClick = {
            Log.d(TAG_UI, "==========================================================================")
            Log.d(TAG_UI, "[LOGIN_CLICK] 🔘 NGƯỜI DÙNG BẤM NÚT ĐĂNG NHẬP & KẾT NỐI")
            Log.d(TAG_UI, " -> Tài khoản nhập vào: ***USER_HIDDEN***") // 🎯 Đã giấu user
            Log.d(TAG_UI, "==========================================================================")

            val prefs = context.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
            prefs.edit()
                .putString("user", NetworkConfig.QUIC_USER)
                .putString("pass", NetworkConfig.QUIC_PASS)
                .apply()

            scope.launch {
                logMsg = "Đang rà soát mạng..."

                // 🎯 [QUAN TRỌNG NHẤT]: Cởi trói VPN từ lần chạy trước để OS dùng mạng mặc định (WiFi)
                NetworkUtils.clearProcessBinding(context)

                // Bắt đầu chọc Ping thử LAN sau khi đã tháo gông cùm
                val isLocal = NetworkUtils.pingHost(NetworkConfig.LOCAL_IP)

                if (isLocal) {
                    NetworkConfig.SERVER_IP = NetworkConfig.LOCAL_IP
                    // Xóa dòng clearProcessBinding ở đây vì ta đã đưa nó lên trên rồi
                    logMsg = "✅ MẠNG LAN KHỎE! Đang dò MTU..."
                } else {
                    NetworkConfig.SERVER_IP = NetworkConfig.TS_IP
                    // Nếu LAN thực sự chết, lúc này mới trói app lại vào hầm VPN
                    NetworkUtils.bindProcessToVpn(context)
                    logMsg = "🔗 KÍCH HOẠT TAILSCALE/NETBIRD! Đang dò MTU..."
                }

                val probeUrl = "https://${NetworkConfig.QUIC_USER}:${NetworkConfig.QUIC_PASS}@${NetworkConfig.SERVER_IP}:${NetworkConfig.QUIC_PORT}/api/list?path=/"

//                val safeProbeUrl = probeUrl
//                    .replace(NetworkConfig.QUIC_USER, "***USER***")
//                    .replace(NetworkConfig.QUIC_PASS, "***PASS***")
//                    .replace(NetworkConfig.SERVER_IP, "đố bạn đoán được :)))")

                Log.d(TAG_UI, "[PROBE_TRIGGER] Kích hoạt chặt nhị phân quét MTU thử nghiệm...")
                Log.d(TAG_UI, " -> URL thăm dò: đố ný bik đấy :)))")

                NetworkConfig.QUIC_MTU = NetworkUtils.discoverBestMtu(probeUrl, !isLocal)

                if (NetworkConfig.QUIC_MTU >= 1200) {
                    logMsg = "✅ Đăng nhập thành công!\nHost: ${NetworkConfig.SERVER_IP.replace(Regex(".*"), "đã giấu :v")} | MTU: ${NetworkConfig.QUIC_MTU}\nVirtual Root: /"
                    Log.d(TAG_UI, "[LOGIN_SUCCESS] 🎉 ĐĂNG NHẬP HOÀN TOÀN THÀNH CÔNG VỚI MTU: ${NetworkConfig.QUIC_MTU}")
                } else {
                    logMsg = "❌ Đăng nhập thất bại (Sai Pass hoặc Server sập)!"
                    Log.e(TAG_UI, "[LOGIN_FAILED] ❌ THẤT BẠI: Server từ chối gói tin hoặc thông tin tài khoản sai!")
                }
                Log.d(TAG_UI, "==========================================================================")
            }

//            scope.launch {
//                logMsg = "Đang rà soát mạng..."
//
//                val isLocal = NetworkUtils.pingHost(NetworkConfig.LOCAL_IP)
//
//                if (isLocal) {
//                    NetworkConfig.SERVER_IP = NetworkConfig.LOCAL_IP
//                    NetworkUtils.clearProcessBinding(context)
//                    logMsg = "✅ MẠNG LAN KHỎE! Đang dò MTU..."
//                } else {
//                    NetworkConfig.SERVER_IP = NetworkConfig.TS_IP
//                    NetworkUtils.bindProcessToVpn(context)
//                    logMsg = "🔗 KÍCH HOẠT TAILSCALE! Đang dò MTU..."
//                }
//
//                val probeUrl = "https://${NetworkConfig.QUIC_USER}:${NetworkConfig.QUIC_PASS}@${NetworkConfig.SERVER_IP}:${NetworkConfig.QUIC_PORT}/api/list?path=/"
//
////                // 🎯 Dùng hàm replace để thay thế toàn bộ thông tin nhạy cảm thành dấu ***
////                val safeProbeUrl = probeUrl
////                    .replace(NetworkConfig.QUIC_USER, "***USER***")
////                    .replace(NetworkConfig.QUIC_PASS, "***PASS***")
////                    .replace(NetworkConfig.SERVER_IP, "***.***.***.***")
//
//                Log.d(TAG_UI, "[PROBE_TRIGGER] Kích hoạt chặt nhị phân quét MTU thử nghiệm...")
//                Log.d(TAG_UI, " -> URL thăm dò: đố ný bik đấy :)))") // 🎯 In ra URL đã được che chắn
//
//                NetworkConfig.QUIC_MTU = NetworkUtils.discoverBestMtu(probeUrl, !isLocal)
//
//                if (NetworkConfig.QUIC_MTU >= 1200) {
//                    // 🎯 Giấu luôn thông tin hiển thị trên UI màn hình điện thoại
//                    logMsg = "✅ Đăng nhập thành công!\nHost: ***.***.***.*** | MTU: ${NetworkConfig.QUIC_MTU}\nVirtual Root: /"
//                    Log.d(TAG_UI, "[LOGIN_SUCCESS] 🎉 ĐĂNG NHẬP HOÀN TOÀN THÀNH CÔNG VỚI MTU: ${NetworkConfig.QUIC_MTU}")
//                } else {
//                    logMsg = "❌ Đăng nhập thất bại (Sai Pass hoặc Server sập)!"
//                    Log.e(TAG_UI, "[LOGIN_FAILED] ❌ THẤT BẠI: Server từ chối gói tin hoặc thông tin tài khoản sai!")
//                }
//                Log.d(TAG_UI, "==========================================================================")
//            }
        }, Modifier.padding(top = 16.dp)) {
            Text("ĐĂNG NHẬP & KẾT NỐI")
        }

        Spacer(Modifier.height(16.dp))
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.DarkGray)) {
            Text(logMsg, Modifier.padding(16.dp), color = Color.Cyan)
        }
    }
}
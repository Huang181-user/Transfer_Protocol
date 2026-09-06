package com.example.transfer_server

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.example.transfer_server.ui.theme.Transfer_serverTheme
import com.example.app.network.RealtimeLogger

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppSecrets.init(this)
        NetworkConfig.LOCAL_IP = AppSecrets.LOCAL_IP
        NetworkConfig.TS_IP = AppSecrets.TS_IP
        NetworkConfig.AUTH_PORT = AppSecrets.AUTH_PORT
        NetworkConfig.SNI_DOMAIN = AppSecrets.SNI_DOMAIN
        NetworkConfig.MASTER_SYM_KEY = AppSecrets.MASTER_SYM_KEY
        val androidId = Settings.Secure.getString(contentResolver, Settings.Secure.ANDROID_ID)
        NetworkConfig.HW_ID = androidId ?: "UNKNOWN_ANDROID_DEVICE"
        val prefs = getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
        NetworkConfig.QUIC_USER = prefs.getString("user", "") ?: ""
        NetworkConfig.QUIC_PASS = prefs.getString("pass", "") ?: ""

        setContent { Transfer_serverTheme { Surface(modifier = Modifier.fillMaxSize()) { QuicSuperScreen() } } }
    }
}

@Composable
fun QuicSuperScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val appLogs by RealtimeLogger.appLogs.collectAsState()

    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("ZHIAUTH V6.0 CLIENT (PURE C++)", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(10.dp))
        OutlinedTextField(NetworkConfig.LOCAL_IP, { NetworkConfig.LOCAL_IP = it }, label = { Text("IP LAN Server") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.TS_IP, { NetworkConfig.TS_IP = it }, label = { Text("IP Tailscale Server") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_USER, { NetworkConfig.QUIC_USER = it }, label = { Text("Tài khoản") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_PASS, { NetworkConfig.QUIC_PASS = it }, label = { Text("Mật khẩu") }, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())

        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), modifier = Modifier.padding(top = 16.dp)) {
            Button(onClick = {
                val prefs = context.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
                prefs.edit().putString("user", NetworkConfig.QUIC_USER).putString("pass", NetworkConfig.QUIC_PASS).apply()
                scope.launch {
                    RealtimeLogger.i("AUTH", "Đang quét định tuyến mạng tối ưu...")

                    var bestIp = ""
                    withContext(Dispatchers.IO) {
                        bestIp = NetworkConfig.discoverBestRouteSync()
                    }
                    NetworkConfig.SERVER_IP = bestIp

                    if (bestIp == NetworkConfig.LOCAL_IP) {
                        RealtimeLogger.i("AUTH", "Định tuyến ưu tiên mạng LAN/Wi-Fi: $bestIp")
                    } else {
                        RealtimeLogger.w("AUTH", "Đảo tuyến sang mạng riêng ảo Tailscale: $bestIp")
                    }

                    // Dùng if-else bọc lại thay vì xài return@launch để né lỗi Kotlin
                    if (NetworkConfig.SERVER_IP.isEmpty() || NetworkConfig.SERVER_IP == "NONE") {
                        RealtimeLogger.e("AUTH", "❌ Không có IP Server nào được cấu hình!")
                    } else {
                        RealtimeLogger.i("AUTH", "Đang gõ cửa UFW Server bằng MsQUIC C++...")
                        val result: String = withContext(Dispatchers.IO) {
                            val deviceIps = ZhiAuthNative.getDeviceIps().split("|")
                            val realLan = deviceIps.getOrNull(0) ?: "NONE"
                            val realTs = deviceIps.getOrNull(1) ?: "NONE"
                            RealtimeLogger.d("AUTH", "IP thật của điện thoại -> LAN: $realLan | TS: $realTs")

                            ZhiAuthNative.shutdownQuic()
                            ZhiAuthNative.initMsQuic(NetworkConfig.SERVER_IP, NetworkConfig.AUTH_PORT.toInt(), NetworkConfig.QUIC_PORT.toInt())

                            val authPayload = "AUTH_REQ|USER:${NetworkConfig.QUIC_USER}|PASS:${NetworkConfig.QUIC_PASS}|LAN:$realLan|TS:$realTs|HWID:${NetworkConfig.HW_ID}"
                            ZhiAuthNative.authMsQuic(authPayload)
                        }

                        if (result.startsWith("AUTH_SUCCESS")) {
                            val parts = result.split("|")
                            ZhiAuthNative.remoteRoot = parts[1]

                            var bestMtu = withContext(Dispatchers.IO) { ZhiAuthNative.discoverMtu(NetworkConfig.SERVER_IP) }

                            if (NetworkConfig.SERVER_IP.startsWith("100.")) {
                                bestMtu = 1200
                                RealtimeLogger.i("AUTH", "Phát hiện đi qua Tailscale, ép MTU = 1200 chống rớt gói!")
                            }
                            NetworkConfig.QUIC_MTU = bestMtu

                            ZhiAuthNative.initKcp(NetworkConfig.SERVER_IP, parts[3].toInt(), NetworkConfig.MASTER_SYM_KEY, bestMtu, parts[4].toInt(), parts[5].toInt(), parts[6].toInt(), parts[7].toInt(), parts[8].toInt(), parts[9].toInt())

                            context.startService(Intent(context, ZhiAuthService::class.java))
                            RealtimeLogger.i("AUTH", "✅ ĐĂNG NHẬP THÀNH CÔNG! Host: ${NetworkConfig.SERVER_IP} | MTU: $bestMtu")
                        } else {
                            RealtimeLogger.e("AUTH", "❌ Lỗi đăng nhập: $result")
                        }
                    }
                }
            }) { Text("KẾT NỐI") }
            Button(onClick = {
                context.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE).edit().clear().apply()
                NetworkConfig.QUIC_USER = ""; NetworkConfig.QUIC_PASS = ""
                scope.launch {
                    withContext(Dispatchers.IO) {
                        ZhiAuthNative.shutdownQuic()
                        ZhiAuthNative.shutdownKcp()
                    }
                    context.stopService(Intent(context, ZhiAuthService::class.java))
                    RealtimeLogger.i("AUTH", "👋 Đã đăng xuất! Tiến trình ngầm đã bị triệt tiêu.")
                }
            }, colors = ButtonDefaults.buttonColors(containerColor = Color.Red)) { Text("ĐĂNG XUẤT") }
        }
        Spacer(Modifier.height(16.dp))
        Card(Modifier.fillMaxWidth().weight(1f), colors = CardDefaults.cardColors(containerColor = Color.DarkGray)) {
            Text(text = appLogs, modifier = Modifier.padding(16.dp).verticalScroll(rememberScrollState()), color = Color.Cyan, style = MaterialTheme.typography.bodySmall)
        }
    }
}
package com.example.transfer_server

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import quicclient.Quicclient
import com.example.transfer_server.ui.theme.Transfer_serverTheme

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

        setContent {
            Transfer_serverTheme {
                Surface(modifier = Modifier.fillMaxSize()) { QuicSuperScreen() }
            }
        }
    }
}

@Composable
fun QuicSuperScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var logMsg by remember { mutableStateOf("Chưa đăng nhập...") }

    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("ZHIAUTH V6.0 CLIENT", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(10.dp))

        OutlinedTextField(NetworkConfig.LOCAL_IP, { NetworkConfig.LOCAL_IP = it }, label = { Text("IP LAN") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.TS_IP, { NetworkConfig.TS_IP = it }, label = { Text("IP Tailscale") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_USER, { NetworkConfig.QUIC_USER = it }, label = { Text("Tài khoản") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(NetworkConfig.QUIC_PASS, { NetworkConfig.QUIC_PASS = it }, label = { Text("Mật khẩu") }, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())

        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), modifier = Modifier.padding(top = 16.dp)) {
            Button(onClick = {
                val prefs = context.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
                prefs.edit().putString("user", NetworkConfig.QUIC_USER).putString("pass", NetworkConfig.QUIC_PASS).apply()

                scope.launch {
                    logMsg = "Đang rà soát mạng và gõ cửa UFW Server..."
                    val result = withContext(Dispatchers.IO) {
                        Quicclient.initializeQUIC(
                            NetworkConfig.LOCAL_IP, NetworkConfig.TS_IP,
                            NetworkConfig.QUIC_USER, NetworkConfig.QUIC_PASS,
                            NetworkConfig.HW_ID, NetworkConfig.AUTH_PORT,
                            NetworkConfig.MASTER_SYM_KEY, NetworkConfig.SNI_DOMAIN
                        )
                    }

                    if (result.startsWith("SUCCESS")) {
                        val parts = result.split("|")
                        NetworkConfig.QUIC_MTU = parts[1].toInt()
                        NetworkConfig.SERVER_IP = parts[2]
                        NetworkConfig.QUIC_PORT = parts[3]
                        NetworkConfig.KCP_PORT = parts[4]
                        if (parts.size > 5) KcpNative.remoteRoot = parts[5]
                        
                        // 🔥 KÍCH HOẠT C++ NDK SAU KHI GO GÕ CỬA XONG
                        val kcpInit = KcpNative.initKcp(NetworkConfig.SERVER_IP, NetworkConfig.KCP_PORT.toInt(), NetworkConfig.MASTER_SYM_KEY, NetworkConfig.QUIC_MTU)
                        
                        context.startService(Intent(context, ZhiAuthService::class.java))

                        logMsg = "✅ ĐĂNG NHẬP THÀNH CÔNG!\nHost: ${NetworkConfig.SERVER_IP}\nQ-Port: ${NetworkConfig.QUIC_PORT} | K-Port: ${NetworkConfig.KCP_PORT}\nC++ KCP Engine: $kcpInit"
                    } else {
                        logMsg = "❌ Lỗi đăng nhập:\n$result"
                    }
                }
            }) { Text("KẾT NỐI") }

            Button(onClick = {
                val prefs = context.getSharedPreferences("HuangQuicPrefs", Context.MODE_PRIVATE)
                prefs.edit().clear().apply()
                NetworkConfig.QUIC_USER = ""; NetworkConfig.QUIC_PASS = ""

                scope.launch {
                    withContext(Dispatchers.IO) { 
                        Quicclient.logout() 
                        KcpNative.shutdownKcp()
                    }
                    context.stopService(Intent(context, ZhiAuthService::class.java))
                    logMsg = "👋 Đã đăng xuất! Tiến trình ngầm đã bị triệt tiêu."
                }
            }, colors = ButtonDefaults.buttonColors(containerColor = Color.Red)) { Text("ĐĂNG XUẤT") }
        }

        Spacer(Modifier.height(16.dp))
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.DarkGray)) {
            Text(logMsg, Modifier.padding(16.dp), color = Color.Cyan)
        }
    }
}

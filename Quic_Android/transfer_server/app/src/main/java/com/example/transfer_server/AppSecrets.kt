package com.example.transfer_server

import android.content.Context
import org.json.JSONObject

object AppSecrets {
    var LOCAL_IP = ""
    var TS_IP = ""
    var QUIC_PORT = "4433"
    var SNI_DOMAIN = ""

    var SFTP_USER = ""
    var SFTP_PASS = ""
    var SFTP_PORT = 22

    var DEFAULT_ROOT_PATH = ""

    fun init(context: Context) {
        try {
            val json = context.assets.open("config.json").bufferedReader().use { it.readText() }
            val obj = JSONObject(json)
            LOCAL_IP = obj.getString("local_ip")
            TS_IP = obj.getString("ts_ip")
            QUIC_PORT = obj.getString("quic_port")
            SNI_DOMAIN = obj.getString("sni_domain")

            SFTP_USER = obj.getString("sftp_user")
            SFTP_PASS = obj.getString("sftp_pass")
            SFTP_PORT = obj.getInt("sftp_port")

            DEFAULT_ROOT_PATH = obj.getString("default_root_path")
        } catch (e: Exception) {
            android.util.Log.e("SECRETS", "❌ Lỗi đọc config.json")
        }
    }
}
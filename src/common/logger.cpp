#include "common/logger.h"
#include <iostream>
#include <windows.h>
#include <chrono>

std::mutex Logger::log_mutex;

std::string Logger::get_realtime_timestamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[50]; sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

// Hàm che giấu thủ công (Không dùng Regex để ép hiệu năng tối đa)
std::string fast_mask_string(const std::string& input) {
    std::string res = input;
    // 1. Che IP (Tìm pattern X.X.X.X đơn giản)
    size_t pos = 0;
    while ((pos = res.find(".", pos)) != std::string::npos) {
        if (pos > 0 && pos + 1 < res.length() && isdigit(res[pos-1]) && isdigit(res[pos+1])) {
            size_t end_ip = res.find(" ", pos);
            if (end_ip == std::string::npos) end_ip = res.length();
            std::string sub = res.substr(pos - 3, end_ip - (pos - 3)); // Khoảng IP
            size_t dot1 = res.find(".", pos-3);
            size_t dot3 = res.rfind(".", end_ip);
            if (dot1 != std::string::npos && dot3 != std::string::npos && dot1 != dot3) {
                for (size_t i = dot1 + 1; i < dot3; i++) { if (isdigit(res[i])) res[i] = '*'; }
            }
        }
        pos++;
    }
    // 2. Che Path (/***/***/FileName)
    pos = 0;
    while ((pos = res.find("/export/", pos)) != std::string::npos) {
        size_t end_path = res.find(" ", pos);
        if (end_path == std::string::npos) end_path = res.length();
        size_t last_slash = res.rfind("/", end_path);
        if (last_slash != std::string::npos && last_slash > pos) {
            std::string masked = "/***/***/" + res.substr(last_slash + 1, end_path - last_slash - 1);
            res.replace(pos, end_path - pos, masked);
        }
        pos++;
    }
    return res;
}

void Logger::log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::string filename = file;
    size_t last_slash = filename.find_last_of("\\/");
    if (last_slash != std::string::npos) filename = filename.substr(last_slash + 1);

    std::string lvl_str = "INFO";
    if (level == LogLevel::DEBUG) lvl_str = "DEBUG";
    else if (level == LogLevel::WARN) lvl_str = "WARN";
    else if (level == LogLevel::ERR) lvl_str = "ERROR";

    std::string final_msg = fast_mask_string(msg);
    std::cout << "[" << get_realtime_timestamp() << "] [" << lvl_str << "] [" << filename << ":" << line << "::" << func << "] -> " << final_msg << std::endl;
}

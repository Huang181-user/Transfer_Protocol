// --- logger.cpp ---
#include "logger.h"
#include <iostream>
#include <windows.h>

std::mutex Logger::log_mutex;

std::string Logger::get_realtime_timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st); // API Windows lấy giờ hệ thống thời gian thực
    char buf[50];
    sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", 
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
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

    std::cout << "[" << get_realtime_timestamp() << "] [" << lvl_str << "] [" << filename << ":" << line << "::" << func << "] -> " << msg << std::endl;
}
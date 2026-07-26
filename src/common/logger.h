#ifndef ZHIAUTH_LOGGER_H
#define ZHIAUTH_LOGGER_H

#include <string>
#include <mutex>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    // Hàm thực thi ghi log cốt lõi
    static void log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& message);

private:
    static std::mutex log_mutex;
    static std::string level_to_string(LogLevel level);
    static std::string get_current_timestamp();
};

// HỆ THỐNG MACRO THÔNG MINH TỰ ĐỘNG BẮT ĐỊNH DANH LUỒNG CHẠY
#define ZHI_LOG_DEBUG(msg) Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_INFO(msg)  Logger::log(LogLevel::INFO,  __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_WARN(msg)  Logger::log(LogLevel::WARNING, __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_ERR(msg)   Logger::log(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, msg)

#endif // ZHIAUTH_LOGGER_H

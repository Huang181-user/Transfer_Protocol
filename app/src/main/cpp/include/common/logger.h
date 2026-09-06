#pragma once
#include <string>
enum class LogLevel { DEBUG, INFO, WARNING, ERROR };
class Logger {
public:
    static void log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& message);
};
#define ZHI_LOG_DEBUG(msg) Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_INFO(msg)  Logger::log(LogLevel::INFO,  __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_WARN(msg)  Logger::log(LogLevel::WARNING, __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_ERR(msg)   Logger::log(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, msg)

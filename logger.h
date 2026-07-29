// --- logger.h ---
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>

enum class LogLevel { DEBUG, INFO, WARN, ERR };

class Logger {
public:
    static void log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& msg);
private:
    static std::mutex log_mutex;
    static std::string get_realtime_timestamp();
};

#define ZHI_LOG_DEBUG(msg) Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_INFO(msg)  Logger::log(LogLevel::INFO,  __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_WARN(msg)  Logger::log(LogLevel::WARN,  __FILE__, __LINE__, __FUNCTION__, msg)
#define ZHI_LOG_ERR(msg)   Logger::log(LogLevel::ERR,   __FILE__, __LINE__, __FUNCTION__, msg)

#endif // LOGGER_H
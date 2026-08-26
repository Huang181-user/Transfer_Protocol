#include "common/logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::mutex Logger::log_mutex;

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

std::string Logger::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm bt{}; localtime_r(&time_t_now, &bt);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << millis;
    return oss.str();
}

void Logger::log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::string filename = file;
    size_t last_slash = filename.find_last_of("\\/");
    if (last_slash != std::string::npos) filename = filename.substr(last_slash + 1);
    std::cout << "[" << get_current_timestamp() << "] [" << level_to_string(level) << "] [" 
              << filename << ":" << line << "::" << func << "] -> " << message << std::endl;
}

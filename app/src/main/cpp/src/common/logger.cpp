#include "common/logger.h"
#include <android/log.h>

void Logger::log(LogLevel level, const std::string& file, int line, const std::string& func, const std::string& message) {
    int android_level = ANDROID_LOG_INFO;
    if (level == LogLevel::DEBUG) android_level = ANDROID_LOG_DEBUG;
    if (level == LogLevel::WARNING) android_level = ANDROID_LOG_WARN;
    if (level == LogLevel::ERROR) android_level = ANDROID_LOG_ERROR;
    
    std::string filename = file;
    size_t last_slash = filename.find_last_of("\\/");
    if (last_slash != std::string::npos) filename = filename.substr(last_slash + 1);
    
    __android_log_print(android_level, "HUANG_C++_CORE", "[%s:%d::%s] -> %s", filename.c_str(), line, func.c_str(), message.c_str());
}

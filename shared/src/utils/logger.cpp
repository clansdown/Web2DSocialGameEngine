#include "logger.h"
#include <iostream>
#include <ctime>

namespace Web2DEngine {
namespace Utils {

void Logger::log(LogLevel level, const std::string& message) {
    // Get current time
    time_t now = time(nullptr);
    char timeStr[26];
    
    // Use cross-platform time formatting
#ifdef _WIN32
    ctime_s(timeStr, sizeof(timeStr), &now);
#else
    ctime_r(&now, timeStr);
#endif
    timeStr[24] = '\0'; // Remove newline
    
    std::string levelStr;
    switch (level) {
        case LogLevel::DEBUG:   levelStr = "DEBUG"; break;
        case LogLevel::INFO:    levelStr = "INFO"; break;
        case LogLevel::WARNING: levelStr = "WARNING"; break;
        case LogLevel::ERROR:   levelStr = "ERROR"; break;
    }
    
    std::cout << "[" << timeStr << "] [" << levelStr << "] " << message << std::endl;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

} // namespace Utils
} // namespace Web2DEngine

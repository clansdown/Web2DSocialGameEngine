#ifndef LOGGER_H
#define LOGGER_H

#include <string>

/**
 * @file logger.h
 * @brief Logging utility for the engine
 */

namespace Web2DEngine {
namespace Utils {

/**
 * @brief Log levels
 */
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

/**
 * @brief Simple logger class
 */
class Logger {
public:
    /**
     * @brief Log a message
     * @param level Log level
     * @param message Message to log
     */
    static void log(LogLevel level, const std::string& message);
    
    /**
     * @brief Log a debug message
     * @param message Message to log
     */
    static void debug(const std::string& message);
    
    /**
     * @brief Log an info message
     * @param message Message to log
     */
    static void info(const std::string& message);
    
    /**
     * @brief Log a warning message
     * @param message Message to log
     */
    static void warning(const std::string& message);
    
    /**
     * @brief Log an error message
     * @param message Message to log
     */
    static void error(const std::string& message);
};

} // namespace Utils
} // namespace Web2DEngine

#endif // LOGGER_H

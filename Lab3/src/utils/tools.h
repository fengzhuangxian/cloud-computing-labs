#ifndef TOOLS_H
#define TOOLS_H

#include <string>
#include <iostream>

namespace raft {

/**
 * 工具函数集合
 */

/**
 * 日志记录函数，支持不同级别的日志输出
 */
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

inline void log(LogLevel level, const std::string& message) {
    switch (level) {
        case LogLevel::DEBUG:
            std::cout << "[DEBUG] " << message << std::endl;
            break;
        case LogLevel::INFO:
            std::cout << "[INFO] " << message << std::endl;
            break;
        case LogLevel::WARNING:
            std::cerr << "[WARNING] " << message << std::endl;
            break;
        case LogLevel::ERROR:
            std::cerr << "[ERROR] " << message << std::endl;
            break;
    }
}

} // namespace raft

#endif // TOOLS_H 
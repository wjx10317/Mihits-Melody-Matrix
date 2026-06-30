/**
 * @file logger.cpp
 * @brief Logger 类的实现
 *
 * 文件职责：
 *   实现日志初始化、格式化输出、时间戳生成及文件 I/O。
 *
 * 主要依赖：
 *   logger.h、<filesystem>（自动创建日志目录）。
 *
 * 在项目中的用法：
 *   由 logger.h 间接链接，业务代码通过 MM_LOG_* 宏调用。
 */
#include "logger.h"

#include <iostream>
#include <filesystem>

namespace melody_matrix::util {

void Logger::init(const std::string& logFilePath, Level minLevel) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_minLevel = minLevel;

    if (!logFilePath.empty()) {
        // 确保日志文件的父目录存在
        std::filesystem::path p(logFilePath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        s_file.open(logFilePath, std::ios::app);
        if (!s_file.is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << logFilePath << std::endl;
        }
    }
    s_initialized = true;
}

void Logger::log(Level level, std::string_view file, int line, std::string_view module,
                 const std::string& message) {
    if (level < s_minLevel) {
        return;
    }

    // 从完整路径中提取文件名，缩短日志行长度
    std::string filename;
    auto pos = file.find_last_of("/\\");
    filename = (pos != std::string_view::npos) ? std::string(file.substr(pos + 1)) : std::string(file);

    std::string lineStr = "[" + timestamp() + "] [" + levelToString(level) + "] [" +
                          std::string(module) + "] " + message + " (" + filename + ":" +
                          std::to_string(line) + ")";

    std::lock_guard<std::mutex> lock(s_mutex);
    // ERROR/FATAL 始终输出到 stderr，便于开发时在控制台即时看到
    if (level >= Level::ERR) {
        std::cerr << lineStr << std::endl;
    }
    if (s_file.is_open()) {
        s_file << lineStr << std::endl;
    }
}

void Logger::logFmt(Level level, std::string_view file, int line, std::string_view module,
                    const char* fmt, ...) {
    if (level < s_minLevel) {
        return;
    }

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    log(level, file, line, module, std::string(buf));
}

void Logger::logFmt(Level level, std::string_view file, int line, std::string_view module,
                    const std::string& message) {
    log(level, file, line, module, message);
}

void Logger::setLevel(Level level) {
    s_minLevel = level;
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file.is_open()) {
        s_file.flush();
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file.is_open()) {
        s_file.flush();
        s_file.close();
    }
    s_initialized = false;
}

std::string Logger::levelToString(Level level) {
    switch (level) {
    case Level::DEBUG: return "DEBUG";
    case Level::INFO:  return "INFO ";
    case Level::WARN:  return "WARN ";
    case Level::ERR:   return "ERROR";
    case Level::FATAL: return "FATAL";
    default:           return "?????";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace melody_matrix::util

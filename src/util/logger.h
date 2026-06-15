#pragma once

#include <string>
#include <string_view>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace melody_matrix::util {

/// 日志严重级别
enum class Level { DEBUG, INFO, WARN, ERR, FATAL };

/// 简单的线程安全日志器。输出到 stderr 和日志文件。
/// 格式: [HH:MM:SS.mmm] [级别] [模块] 消息
class Logger {
public:
    /// 使用可选的日志文件路径初始化日志器。
    /// 如果路径为空，仅输出到 stderr。
    static void init(const std::string& logFilePath = "logs/melody_matrix.log",
                     Level minLevel = Level::INFO);

    /// 在给定级别记录消息（已格式化字符串）
    static void log(Level level, std::string_view file, int line, std::string_view module,
                    const std::string& message);

    /// 在给定级别记录消息（printf 风格格式化）
    static void logFmt(Level level, std::string_view file, int line, std::string_view module,
                       const char* fmt, ...);

    /// std::string 重载：直接透传，不做 printf 格式化
    static void logFmt(Level level, std::string_view file, int line, std::string_view module,
                       const std::string& message);

    /// 设置最小日志级别（低于此级别的消息被丢弃）
    static void setLevel(Level level);

    /// 刷新日志文件
    static void flush();

    /// 关闭日志器（关闭文件）
    static void shutdown();

private:
    static std::string levelToString(Level level);
    static std::string timestamp();

    static inline Level s_minLevel = Level::INFO;
    static inline std::mutex s_mutex;
    static inline std::ofstream s_file;
    static inline bool s_initialized = false;
};

} // namespace melody_matrix::util

// ── 便捷宏（支持 printf 风格变参格式化）──
#define MM_LOG_DEBUG(module, fmt, ...) \
    melody_matrix::util::Logger::logFmt(melody_matrix::util::Level::DEBUG, __FILE__, __LINE__, module, fmt, ##__VA_ARGS__)
#define MM_LOG_INFO(module, fmt, ...) \
    melody_matrix::util::Logger::logFmt(melody_matrix::util::Level::INFO, __FILE__, __LINE__, module, fmt, ##__VA_ARGS__)
#define MM_LOG_WARN(module, fmt, ...) \
    melody_matrix::util::Logger::logFmt(melody_matrix::util::Level::WARN, __FILE__, __LINE__, module, fmt, ##__VA_ARGS__)
#define MM_LOG_ERROR(module, fmt, ...) \
    melody_matrix::util::Logger::logFmt(melody_matrix::util::Level::ERR, __FILE__, __LINE__, module, fmt, ##__VA_ARGS__)
#define MM_LOG_FATAL(module, fmt, ...) \
    melody_matrix::util::Logger::logFmt(melody_matrix::util::Level::FATAL, __FILE__, __LINE__, module, fmt, ##__VA_ARGS__)

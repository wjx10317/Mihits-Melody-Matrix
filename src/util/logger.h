/**
 * @file logger.h
 * @brief 线程安全日志系统
 *
 * 文件职责：
 *   定义日志级别、Logger 静态类及 MM_LOG_* 宏，统一全项目日志输出格式。
 *
 * 主要依赖：
 *   标准库（mutex、fstream、chrono 等）；无外部日志框架。
 *
 * 在项目中的用法：
 *   main.cpp 中 Logger::init() 启动日志；各模块通过 MM_LOG_INFO("Module", "msg %d", x) 记录。
 *   输出格式：[HH:MM:SS.mmm] [级别] [模块] 消息 (文件名:行号)
 */
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

/** @brief 日志严重级别，数值越大越严重 */
enum class Level { DEBUG, INFO, WARN, ERR, FATAL };

/**
 * @brief 简单的线程安全日志器
 *
 * 同时输出到 stderr（ERROR/FATAL 级别）和日志文件。
 * 所有方法均为静态，全局共享一个文件句柄与互斥锁。
 */
class Logger {
public:
    /**
     * @brief 初始化日志器
     * @param logFilePath 日志文件路径，空字符串则仅输出到 stderr
     * @param minLevel 最低记录级别，低于此级别的消息被丢弃
     */
    static void init(const std::string& logFilePath = "logs/melody_matrix.log",
                     Level minLevel = Level::INFO);

    /**
     * @brief 记录已格式化的消息
     * @param level 日志级别
     * @param file 源文件名（通常传 __FILE__）
     * @param line 源行号（通常传 __LINE__）
     * @param module 模块名标识
     * @param message 完整消息文本
     */
    static void log(Level level, std::string_view file, int line, std::string_view module,
                    const std::string& message);

    /**
     * @brief 记录 printf 风格格式化的消息
     * @param level 日志级别
     * @param file 源文件名
     * @param line 源行号
     * @param module 模块名
     * @param fmt printf 格式字符串
     * @param ... 格式参数
     */
    static void logFmt(Level level, std::string_view file, int line, std::string_view module,
                       const char* fmt, ...);

    /**
     * @brief std::string 重载：直接透传，不做 printf 格式化
     * @param level 日志级别
     * @param file 源文件名
     * @param line 源行号
     * @param module 模块名
     * @param message 消息字符串
     */
    static void logFmt(Level level, std::string_view file, int line, std::string_view module,
                       const std::string& message);

    /**
     * @brief 设置最小日志级别
     * @param level 新的最低级别
     */
    static void setLevel(Level level);

    /** @brief 刷新日志文件缓冲区 */
    static void flush();

    /** @brief 关闭日志器并释放文件句柄 */
    static void shutdown();

private:
    /**
     * @brief 将级别枚举转为固定宽度字符串
     * @param level 日志级别
     * @return 如 "INFO "、"ERROR"
     */
    static std::string levelToString(Level level);

    /** @brief 生成当前本地时间的 HH:MM:SS.mmm 时间戳 */
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

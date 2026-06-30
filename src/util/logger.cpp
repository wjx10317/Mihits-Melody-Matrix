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
#include "logger.h"  // Logger 类声明

#include <iostream>    // std::cerr 错误输出
#include <filesystem>  // 自动创建日志目录

namespace melody_matrix::util {  // 工具层命名空间

void Logger::init(const std::string& logFilePath, Level minLevel) {
    std::lock_guard<std::mutex> lock(s_mutex);  // 加锁保护全局状态
    s_minLevel = minLevel;  // 设置最低日志级别

    if (!logFilePath.empty()) {  // 指定了日志文件路径
        // 确保日志文件的父目录存在
        std::filesystem::path p(logFilePath);  // 路径对象
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());  // 递归创建父目录
        }
        s_file.open(logFilePath, std::ios::app);  // 以追加模式打开日志文件
        if (!s_file.is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << logFilePath << std::endl;  // 打开失败提示
        }
    }
    s_initialized = true;  // 标记已初始化
}

void Logger::log(Level level, std::string_view file, int line, std::string_view module,
                 const std::string& message) {
    if (level < s_minLevel) {
        return;  // 低于最低级别则丢弃
    }

    // 从完整路径中提取文件名，缩短日志行长度
    std::string filename;  // 短文件名
    auto pos = file.find_last_of("/\\");  // 查找路径分隔符
    filename = (pos != std::string_view::npos) ? std::string(file.substr(pos + 1)) : std::string(file);  // 提取文件名

    std::string lineStr = "[" + timestamp() + "] [" + levelToString(level) + "] [" +
                          std::string(module) + "] " + message + " (" + filename + ":" +
                          std::to_string(line) + ")";  // 组装完整日志行

    std::lock_guard<std::mutex> lock(s_mutex);  // 加锁保护并发写入
    // ERROR/FATAL 始终输出到 stderr，便于开发时在控制台即时看到
    if (level >= Level::ERR) {
        std::cerr << lineStr << std::endl;  // 错误级别输出到 stderr
    }
    if (s_file.is_open()) {
        s_file << lineStr << std::endl;  // 写入日志文件
    }
}

void Logger::logFmt(Level level, std::string_view file, int line, std::string_view module,
                    const char* fmt, ...) {
    if (level < s_minLevel) {
        return;  // 低于最低级别则丢弃
    }

    char buf[2048];  // 格式化缓冲区
    va_list args;    // 变参列表
    va_start(args, fmt);  // 初始化变参
    vsnprintf(buf, sizeof(buf), fmt, args);  // printf 风格格式化
    va_end(args);  // 结束变参

    log(level, file, line, module, std::string(buf));  // 委托给 log() 输出
}

void Logger::logFmt(Level level, std::string_view file, int line, std::string_view module,
                    const std::string& message) {
    log(level, file, line, module, message);  // string 重载直接透传
}

void Logger::setLevel(Level level) {
    s_minLevel = level;  // 更新最低日志级别
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(s_mutex);  // 加锁
    if (s_file.is_open()) {
        s_file.flush();  // 刷新文件缓冲区
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);  // 加锁
    if (s_file.is_open()) {
        s_file.flush();  // 刷新缓冲区
        s_file.close();  // 关闭文件
    }
    s_initialized = false;  // 清除初始化标志
}

std::string Logger::levelToString(Level level) {
    switch (level) {
    case Level::DEBUG: return "DEBUG";  // 调试级别
    case Level::INFO:  return "INFO ";  // 信息级别（固定宽度）
    case Level::WARN:  return "WARN ";  // 警告级别
    case Level::ERR:   return "ERROR";  // 错误级别
    case Level::FATAL: return "FATAL";  // 致命级别
    default:           return "?????";  // 未知级别占位
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();  // 当前系统时间点
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;  // 毫秒部分
    auto t = std::chrono::system_clock::to_time_t(now);  // 转为 time_t
    std::tm tm_buf{};  // 本地时间结构
#ifdef _WIN32
    localtime_s(&tm_buf, &t);  // Windows 线程安全 localtime
#else
    localtime_r(&t, &tm_buf);  // POSIX 线程安全 localtime
#endif
    std::ostringstream oss;  // 字符串流
    oss << std::put_time(&tm_buf, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();  // HH:MM:SS.mmm
    return oss.str();  // 返回时间戳字符串
}

} // namespace melody_matrix::util

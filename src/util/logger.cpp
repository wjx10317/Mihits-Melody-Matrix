#include "logger.h"

#include <iostream>
#include <filesystem>

namespace melody_matrix::util {

void Logger::init(const std::string& logFilePath, Level minLevel) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_minLevel = minLevel;

    if (!logFilePath.empty()) {
        // Ensure parent directory exists
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

    // Extract just the filename from the full path
    std::string filename;
    auto pos = file.find_last_of("/\\");
    filename = (pos != std::string_view::npos) ? std::string(file.substr(pos + 1)) : std::string(file);

    std::string lineStr = "[" + timestamp() + "] [" + levelToString(level) + "] [" +
                          std::string(module) + "] " + message + " (" + filename + ":" +
                          std::to_string(line) + ")";

    std::lock_guard<std::mutex> lock(s_mutex);
    // Always output to stderr for ERROR/FATAL
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

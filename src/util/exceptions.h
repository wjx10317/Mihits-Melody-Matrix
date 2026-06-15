#pragma once

#include <stdexcept>
#include <string>

namespace melody_matrix::util {

/// 所有 Melody Matrix 错误的基异常
class MMException : public std::runtime_error {
public:
    explicit MMException(const std::string& msg) : std::runtime_error(msg) {}
    explicit MMException(const char* msg) : std::runtime_error(msg) {}
};

/// 致命错误：无法恢复，必须退出（例如 GPU 不支持 GL 3.3）
class FatalException : public MMException {
public:
    explicit FatalException(const std::string& msg) : MMException(msg) {}
};

/// 严重错误：可通过状态更改恢复（例如音频设备故障）
class SevereException : public MMException {
public:
    explicit SevereException(const std::string& msg) : MMException(msg) {}
};

/// I/O 或文件相关错误
class IOException : public MMException {
public:
    explicit IOException(const std::string& msg) : MMException(msg) {}
};

/// 谱面解析/验证错误
class BeatmapException : public MMException {
public:
    explicit BeatmapException(const std::string& msg) : MMException(msg) {}
};

} // namespace melody_matrix::util

/**
 * @file exceptions.h
 * @brief Melody Matrix 异常类型层次
 *
 * 文件职责：
 *   定义项目专用的异常基类及按严重程度和域划分的子类。
 *
 * 主要依赖：
 *   标准库 <stdexcept>、<string>。
 *
 * 在项目中的用法：
 *   不可恢复错误抛出 FatalException（main.cpp 捕获并退出）；
 *   谱面解析失败抛 BeatmapException；文件 I/O 失败抛 IOException。
 */
#pragma once  // 防止头文件重复包含

#include <stdexcept>  // std::runtime_error 基类
#include <string>     // 错误消息字符串

namespace melody_matrix::util {  // 工具层命名空间

/**
 * @brief 所有 Melody Matrix 错误的基异常
 *
 * 继承 std::runtime_error，携带可读错误消息。
 */
class MMException : public std::runtime_error {
public:
    /**
     * @param msg 错误描述
     */
    explicit MMException(const std::string& msg) : std::runtime_error(msg) {}  // 从 string 构造
    explicit MMException(const char* msg) : std::runtime_error(msg) {}  // 从 C 字符串构造
};

/**
 * @brief 致命错误：无法恢复，必须退出
 *
 * 例如 GPU 不支持 OpenGL 3.3 Core。
 */
class FatalException : public MMException {
public:
    explicit FatalException(const std::string& msg) : MMException(msg) {}  // 构造致命异常
};

/**
 * @brief 严重错误：可通过状态切换等方式恢复
 *
 * 例如音频设备临时故障。
 */
class SevereException : public MMException {
public:
    explicit SevereException(const std::string& msg) : MMException(msg) {}  // 构造严重异常
};

/**
 * @brief I/O 或文件相关错误
 */
class IOException : public MMException {
public:
    explicit IOException(const std::string& msg) : MMException(msg) {}  // 构造 I/O 异常
};

/**
 * @brief 谱面解析或验证错误
 */
class BeatmapException : public MMException {
public:
    explicit BeatmapException(const std::string& msg) : MMException(msg) {}  // 构造谱面异常
};

} // namespace melody_matrix::util

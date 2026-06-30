/**
 * @file config.cpp
 * @brief Config 类实现
 *
 * 文件职责：
 *   解析/写入 key=value 格式配置文件，提供类型化读写。
 *
 * 主要依赖：
 *   config.h、util/logger.h、util/error_codes.h、<fstream>。
 *
 * 在项目中的用法：
 *   由 config.h 间接链接；Kernel 与设置 UI 通过静态方法访问。
 */
#include "config.h"              // Config 类声明
#include "util/logger.h"         // MM_LOG_* 日志宏
#include "util/error_codes.h"    // ErrorCode 枚举

#include <fstream>   // 文件读写流
#include <sstream>   // 字符串流（预留）

namespace melody_matrix::platform {  // 平台层命名空间

util::Result<void> Config::load(const std::string& path) {
    s_path = path;      // 记录配置文件路径
    s_data.clear();     // 清空旧配置

    std::ifstream file(path);  // 以只读方式打开文件
    if (!file.is_open()) {
        // 配置文件尚不存在 — 使用默认值
        MM_LOG_INFO("Config", "Config file not found, using defaults: " + path);  // 记录使用默认
        s_loaded = true;  // 标记已加载（空配置）
        return util::success();  // 文件不存在仍视为成功
    }

    std::string line;  // 当前读取行
    while (std::getline(file, line)) {  // 逐行读取
        // 跳过空行和注释行（# 或 ; 开头）
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;  // 跳过本行
        }
        auto eq = line.find('=');  // 查找键值分隔符
        if (eq == std::string::npos) {
            continue;  // 无等号则跳过
        }
        std::string key = line.substr(0, eq);       // 提取键
        std::string value = line.substr(eq + 1);    // 提取值
        // 去除键值两端空白
        while (!key.empty() && key.back() == ' ') key.pop_back();           // 去除键尾空格
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());  // 去除值首空格
        s_data[key] = value;  // 存入映射表
    }

    s_loaded = true;  // 标记加载完成
    MM_LOG_INFO("Config", "Config loaded: " + path);  // 记录加载成功
    return util::success();  // 返回成功
}

util::Result<void> Config::save() {
    std::ofstream file(s_path);  // 以写入方式打开配置文件
    if (!file.is_open()) {
        return util::failure<void>(static_cast<int32_t>(util::ErrorCode::ERROR_IO),
                                    "Failed to save config: " + s_path);  // IO 失败
    }

    file << "# Melody Matrix Configuration\n";           // 写入文件头注释
    file << "# Auto-generated — edit at your own risk\n\n";  // 写入警告注释
    for (const auto& [key, value] : s_data) {  // 遍历全部键值对
        file << key << "=" << value << "\n";   // 写入 key=value 行
    }
    file.flush();  // 刷新缓冲区到磁盘

    MM_LOG_INFO("Config", "Config saved: " + s_path);  // 记录保存成功
    return util::success();  // 返回成功
}

int32_t Config::getInt(const std::string& key, int32_t defaultValue) {
    auto it = s_data.find(key);  // 查找键
    if (it == s_data.end()) return defaultValue;  // 不存在则返回默认值
    try {
        return std::stoi(it->second);  // 字符串转整型
    } catch (...) {
        return defaultValue;  // 解析失败返回默认值
    }
}

float Config::getFloat(const std::string& key, float defaultValue) {
    auto it = s_data.find(key);  // 查找键
    if (it == s_data.end()) return defaultValue;  // 不存在则返回默认值
    try {
        return std::stof(it->second);  // 字符串转浮点
    } catch (...) {
        return defaultValue;  // 解析失败返回默认值
    }
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) {
    auto it = s_data.find(key);  // 查找键
    return (it != s_data.end()) ? it->second : defaultValue;  // 存在返回值，否则默认
}

void Config::setInt(const std::string& key, int32_t value) {
    s_data[key] = std::to_string(value);  // 整型转字符串写入
}

void Config::setFloat(const std::string& key, float value) {
    s_data[key] = std::to_string(value);  // 浮点转字符串写入
}

void Config::setString(const std::string& key, const std::string& value) {
    s_data[key] = value;  // 直接写入字符串
}

} // namespace melody_matrix::platform

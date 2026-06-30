/**
 * @file file_system.cpp
 * @brief FileSystem 类实现
 *
 * 文件职责：
 *   实现路径遍历防护、文件存在性检查及整文件读取。
 *
 * 主要依赖：
 *   file_system.h、util/logger.h、util/error_codes.h、<filesystem>。
 *
 * 在项目中的用法：
 *   由 file_system.h 间接链接；谱面解析与资源加载模块调用 safeResolve/readFile。
 */
#include "file_system.h"       // FileSystem 类声明
#include "util/logger.h"       // MM_LOG_* 日志宏
#include "util/error_codes.h"  // ErrorCode 枚举

#include <filesystem>  // std::filesystem 路径操作
#include <fstream>     // 文件读写流

namespace melody_matrix::platform {  // 平台层命名空间

util::Result<std::string> FileSystem::safeResolve(const std::string& basePath,
                                                   const std::string& userPath) {
    // 拒绝绝对路径（Unix /、Windows \ 或盘符 C:）
    if (!userPath.empty() && (userPath[0] == '/' || userPath[0] == '\\' ||
                              (userPath.size() >= 2 && userPath[1] == ':'))) {
        MM_LOG_WARN("FileSystem", "Rejected absolute path: " + userPath);  // 记录拒绝原因
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Absolute paths are not allowed: " + userPath);  // 返回路径遍历错误
    }

    // 拒绝显式 ".." 路径遍历
    std::string normalized = userPath;  // 用户路径副本
    if (normalized.find("..") != std::string::npos) {
        MM_LOG_WARN("FileSystem", "Rejected path traversal: " + userPath);  // 记录拒绝原因
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Path traversal detected: " + userPath);  // 返回路径遍历错误
    }

    // 拼接并尝试规范化路径
    std::filesystem::path base(basePath);           // 基础目录路径对象
    std::filesystem::path full = base / userPath;   // 拼接完整路径
    std::error_code ec;                             // 错误码（不抛异常）
    auto canonical = std::filesystem::weakly_canonical(full, ec);  // 弱规范化
    if (ec) {
        // 路径尚不存在 — 仍用 full 做前缀检查
        canonical = full;  // 回退到拼接路径
    }

    // 验证最终路径仍在 basePath 之下
    auto canonicalBase = std::filesystem::weakly_canonical(base, ec);  // 规范化基础目录
    if (ec) canonicalBase = base;  // 失败则用原始 base

    auto baseStr = canonicalBase.string();  // 基础目录字符串
    auto fullStr = canonical.string();      // 完整路径字符串
    if (fullStr.size() < baseStr.size() ||
        fullStr.substr(0, baseStr.size()) != baseStr) {
        MM_LOG_WARN("FileSystem", "Path escapes base: " + userPath);  // 路径逃出根目录
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Path escapes base directory: " + userPath);  // 返回路径遍历错误
    }

    return canonical.string();  // 返回安全规范化路径
}

bool FileSystem::fileExists(const std::string& path) {
    return std::filesystem::exists(path);  // 检查路径是否存在（文件或目录）
}

bool FileSystem::dirExists(const std::string& path) {
    return std::filesystem::is_directory(path);  // 检查是否为目录
}

util::Result<std::string> FileSystem::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);  // 二进制模式，打开后定位到末尾
    if (!file.is_open()) {
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_FILE_NOT_FOUND),
            "File not found: " + path);  // 文件无法打开
    }
    auto size = file.tellg();  // 获取文件大小
    file.seekg(0);             // 回到文件开头
    std::string content(static_cast<size_t>(size), '\0');  // 预分配缓冲区
    file.read(content.data(), size);  // 一次性读取全部内容
    return content;  // 返回文件内容
}

std::string FileSystem::getAppDir() {
    return std::filesystem::current_path().string();  // 返回当前工作目录
}

void FileSystem::cleanupTemp() {
    std::error_code ec;  // 错误码（不抛异常）
    std::filesystem::remove_all("temp", ec);  // 递归删除 temp 目录
    if (ec) {
        MM_LOG_WARN("FileSystem", "Failed to cleanup temp directory");  // 删除失败
    } else {
        MM_LOG_INFO("FileSystem", "Temp directory cleaned up");  // 删除成功
    }
}

} // namespace melody_matrix::platform

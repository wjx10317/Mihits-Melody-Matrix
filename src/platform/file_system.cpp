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
#include "file_system.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <filesystem>
#include <fstream>

namespace melody_matrix::platform {

util::Result<std::string> FileSystem::safeResolve(const std::string& basePath,
                                                   const std::string& userPath) {
    // 拒绝绝对路径（Unix /、Windows \ 或盘符 C:）
    if (!userPath.empty() && (userPath[0] == '/' || userPath[0] == '\\' ||
                              (userPath.size() >= 2 && userPath[1] == ':'))) {
        MM_LOG_WARN("FileSystem", "Rejected absolute path: " + userPath);
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Absolute paths are not allowed: " + userPath);
    }

    // 拒绝显式 ".." 路径遍历
    std::string normalized = userPath;
    if (normalized.find("..") != std::string::npos) {
        MM_LOG_WARN("FileSystem", "Rejected path traversal: " + userPath);
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Path traversal detected: " + userPath);
    }

    // 拼接并尝试规范化路径
    std::filesystem::path base(basePath);
    std::filesystem::path full = base / userPath;
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(full, ec);
    if (ec) {
        // 路径尚不存在 — 仍用 full 做前缀检查
        canonical = full;
    }

    // 验证最终路径仍在 basePath 之下
    auto canonicalBase = std::filesystem::weakly_canonical(base, ec);
    if (ec) canonicalBase = base;

    auto baseStr = canonicalBase.string();
    auto fullStr = canonical.string();
    if (fullStr.size() < baseStr.size() ||
        fullStr.substr(0, baseStr.size()) != baseStr) {
        MM_LOG_WARN("FileSystem", "Path escapes base: " + userPath);
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Path escapes base directory: " + userPath);
    }

    return canonical.string();
}

bool FileSystem::fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool FileSystem::dirExists(const std::string& path) {
    return std::filesystem::is_directory(path);
}

util::Result<std::string> FileSystem::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_FILE_NOT_FOUND),
            "File not found: " + path);
    }
    auto size = file.tellg();
    file.seekg(0);
    std::string content(static_cast<size_t>(size), '\0');
    file.read(content.data(), size);
    return content;
}

std::string FileSystem::getAppDir() {
    return std::filesystem::current_path().string();
}

void FileSystem::cleanupTemp() {
    std::error_code ec;
    std::filesystem::remove_all("temp", ec);
    if (ec) {
        MM_LOG_WARN("FileSystem", "Failed to cleanup temp directory");
    } else {
        MM_LOG_INFO("FileSystem", "Temp directory cleaned up");
    }
}

} // namespace melody_matrix::platform

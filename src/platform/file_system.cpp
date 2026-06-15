#include "file_system.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <filesystem>
#include <fstream>

namespace melody_matrix::platform {

util::Result<std::string> FileSystem::safeResolve(const std::string& basePath,
                                                   const std::string& userPath) {
    // Reject absolute paths
    if (!userPath.empty() && (userPath[0] == '/' || userPath[0] == '\\' ||
                              (userPath.size() >= 2 && userPath[1] == ':'))) {
        MM_LOG_WARN("FileSystem", "Rejected absolute path: " + userPath);
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Absolute paths are not allowed: " + userPath);
    }

    // Check for ".." path traversal
    std::string normalized = userPath;
    if (normalized.find("..") != std::string::npos) {
        MM_LOG_WARN("FileSystem", "Rejected path traversal: " + userPath);
        return util::failure<std::string>(
            static_cast<int32_t>(util::ErrorCode::ERROR_PATH_TRAVERSAL),
            "Path traversal detected: " + userPath);
    }

    // Build the full path and canonicalize
    std::filesystem::path base(basePath);
    std::filesystem::path full = base / userPath;
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(full, ec);
    if (ec) {
        // Path doesn't exist yet — just check it stays under base
        canonical = full;
    }

    // Verify the resolved path is under basePath
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

#pragma once

#include <string>
#include <vector>

namespace melody_matrix::platform {

/// ZIP 压缩包解压工具。
/// Windows 上使用 PowerShell Expand-Archive 实现，零外部依赖。
class ZipExtract {
public:
    /// 解压 ZIP 文件到指定目录。
    /// @param zipPath  ZIP 文件路径（.osz 等）
    /// @param destDir  目标目录（如不存在会创建）
    /// @return 成功或错误
    static bool extract(const std::string& zipPath, const std::string& destDir);

    /// 创建一个临时目录用于解压，路径基于系统 TEMP + 随机后缀。
    /// @return 临时目录的完整路径
    static std::string createTempDir();

    /// 递归删除指定目录（用于清理临时解压目录）。
    /// @param dirPath 目录路径
    static void removeDir(const std::string& dirPath);

    /// 在指定目录下递归查找所有 .osu 文件。
    /// @param dirPath 搜索根目录
    /// @return 找到的 .osu 文件完整路径列表
    static std::vector<std::string> findOsuFiles(const std::string& dirPath);
};

} // namespace melody_matrix::platform

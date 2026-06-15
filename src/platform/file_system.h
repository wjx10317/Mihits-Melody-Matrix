#pragma once

#include "util/result.h"
#include <string>

namespace melody_matrix::platform {

/// 安全文件系统工具。
/// Melody Matrix 中的所有文件访问必须通过此类进行
/// 以防止路径遍历攻击。
class FileSystem {
public:
    /// 将用户提供的路径解析到安全的基础目录下。
    /// 拒绝：绝对路径、包含 ".." 的路径、逃离基础目录的路径。
    /// 成功时返回安全的规范化路径。
    static util::Result<std::string> safeResolve(const std::string& basePath,
                                                  const std::string& userPath);

    /// 检查文件是否存在
    static bool fileExists(const std::string& path);

    /// 检查目录是否存在
    static bool dirExists(const std::string& path);

    /// 读取整个文件内容到字符串
    static util::Result<std::string> readFile(const std::string& path);

    /// 获取应用程序的基础目录（可执行文件所在位置）
    static std::string getAppDir();

    /// 退出时清理临时文件
    static void cleanupTemp();
};

} // namespace melody_matrix::platform

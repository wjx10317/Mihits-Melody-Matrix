/**
 * @file file_system.h
 * @brief 安全文件系统工具
 *
 * 文件职责：
 *   提供路径安全校验、文件读写及应用目录查询，防止路径遍历攻击。
 *
 * 主要依赖：
 *   util/result.h、标准库 <string>。
 *
 * 在项目中的用法：
 *   谱面/资源加载前必须通过 FileSystem::safeResolve(base, userPath) 规范化路径；
 *   main 退出时调用 cleanupTemp() 清理临时目录。
 */
#pragma once

#include "util/result.h"
#include <string>

namespace melody_matrix::platform {

/**
 * @brief 安全文件系统工具
 *
 * Melody Matrix 中所有涉及用户或谱面内路径的文件访问均应通过此类，
 * 拒绝绝对路径、".." 及逃出 base 目录的路径。
 */
class FileSystem {
public:
    /**
     * @brief 将用户路径解析到安全的基础目录下
     * @param basePath 允许访问的根目录
     * @param userPath 用户或谱面内提供的相对路径
     * @return 成功时返回规范化后的安全路径；失败返回 ERROR_PATH_TRAVERSAL
     */
    static util::Result<std::string> safeResolve(const std::string& basePath,
                                                  const std::string& userPath);

    /**
     * @brief 检查文件是否存在
     * @param path 文件路径
     * @return 存在 true
     */
    static bool fileExists(const std::string& path);

    /**
     * @brief 检查目录是否存在
     * @param path 目录路径
     * @return 是目录 true
     */
    static bool dirExists(const std::string& path);

    /**
     * @brief 读取整个文件为字符串（二进制模式）
     * @param path 文件路径
     * @return 成功时文件内容；失败 ERROR_FILE_NOT_FOUND
     */
    static util::Result<std::string> readFile(const std::string& path);

    /**
     * @brief 获取应用程序基础目录
     * @return 当前工作目录字符串（main 中已设为 exe 所在目录）
     */
    static std::string getAppDir();

    /**
     * @brief 退出时清理 temp 临时目录
     */
    static void cleanupTemp();
};

} // namespace melody_matrix::platform

/**
 * @file zip_extract.h
 * @brief ZIP 压缩包解压工具接口
 *
 * 文件职责：
 *   声明 .osz/.zip 解压、临时目录管理及 .osu 文件搜索的静态 API。
 *
 * 主要依赖：
 *   标准库 <string>、<vector>；实现在 zip_extract.cpp（Windows 系统工具）。
 *
 * 在项目中的用法：
 *   谱面导入流程中调用 ZipExtract::extract() 解压 .osz，
 *   findOsuFiles() 定位谱面文件，removeDir() 清理临时目录。
 */
#pragma once

#include <string>
#include <vector>

namespace melody_matrix::platform {

/**
 * @brief ZIP 压缩包解压工具
 *
 * Windows 上使用 tar.exe 或 PowerShell .NET ZipFile，零第三方依赖。
 * 所有方法均为静态，无需实例化。
 */
class ZipExtract {
public:
    /**
     * @brief 解压 ZIP 文件到指定目录
     * @param zipPath ZIP 文件路径（.osz 等）
     * @param destDir 目标目录（不存在时会创建）
     * @return 成功 true，全部策略失败 false
     */
    static bool extract(const std::string& zipPath, const std::string& destDir);

    /**
     * @brief 创建用于解压的临时目录
     * @return 临时目录完整路径；失败返回空字符串
     *
     * 路径格式：系统 TEMP/melody_matrix_import_<随机后缀>
     */
    static std::string createTempDir();

    /**
     * @brief 递归删除指定目录
     * @param dirPath 目录路径
     *
     * 用于导入完成后清理临时解压目录，失败仅记录警告。
     */
    static void removeDir(const std::string& dirPath);

    /**
     * @brief 在目录下递归查找所有 .osu 文件
     * @param dirPath 搜索根目录
     * @return 找到的 .osu 完整路径列表（按文件名排序）
     */
    static std::vector<std::string> findOsuFiles(const std::string& dirPath);
};

} // namespace melody_matrix::platform

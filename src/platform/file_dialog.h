/**
 * @file file_dialog.h
 * @brief 原生文件选择对话框封装
 *
 * 文件职责：
 *   声明单选/多选文件对话框的跨平台接口（当前实现 Windows）。
 *
 * 主要依赖：
 *   标准库 <string>、<vector>。
 *
 * 在项目中的用法：
 *   谱面导入 UI 调用 FileDialog::openFile() 或 openFiles() 让用户选择 .osu/.osz。
 */
#pragma once

#include <string>
#include <vector>

namespace melody_matrix::platform {

/**
 * @brief 原生文件对话框封装
 *
 * Windows 使用 Comdlg32 GetOpenFileNameW；非 Windows 平台待实现。
 */
class FileDialog {
public:
    /**
     * @brief 打开单选文件对话框
     * @param title 对话框标题
     * @param filterDesc 过滤器描述（如 "osu! Beatmap Files"）
     * @param filterExt 扩展名（如 "osu"）
     * @return 选中文件的完整 UTF-8 路径；取消或失败返回空字符串
     */
    static std::string openFile(const std::string& title,
                                const std::string& filterDesc,
                                const std::string& filterExt);

    /**
     * @brief 打开多选文件对话框
     * @param title 对话框标题
     * @param filterDesc 过滤器描述
     * @param filterExt 扩展名
     * @return 选中文件路径列表；取消返回空列表
     */
    static std::vector<std::string> openFiles(const std::string& title,
                                               const std::string& filterDesc,
                                               const std::string& filterExt);
};

} // namespace melody_matrix::platform

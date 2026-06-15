#pragma once

#include <string>
#include <vector>

namespace melody_matrix::platform {

/// 原生文件对话框封装。
/// Windows 使用 GetOpenFileName (Comdlg32)，跨平台可替换为 tinyfiledialogs 等。
class FileDialog {
public:
    /// 打开"选择文件"对话框（单选）。
    /// @param title 对话框标题
    /// @param filterDesc 过滤器描述（如 "osu! Beatmap Files"）
    /// @param filterExt 过滤器扩展名（如 "osu"）
    /// @return 选中的文件完整路径，取消或失败返回空字符串
    static std::string openFile(const std::string& title,
                                const std::string& filterDesc,
                                const std::string& filterExt);

    /// 打开"选择文件"对话框（多选）。
    /// @return 选中的文件路径列表，取消返回空列表
    static std::vector<std::string> openFiles(const std::string& title,
                                               const std::string& filterDesc,
                                               const std::string& filterExt);
};

} // namespace melody_matrix::platform

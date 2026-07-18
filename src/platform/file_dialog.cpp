/**
 * @file file_dialog.cpp
 * @brief FileDialog 实现（Windows Comdlg32）
 *
 * 文件职责：
 *   封装 GetOpenFileNameW，处理宽字符与 UTF-8 转换及多选结果解析。
 *
 * 主要依赖：
 *   file_dialog.h、util/logger.h、Windows commdlg.h。
 *
 * 在项目中的用法：
 *   由 song_select 等 UI 模块调用，用户无需直接引用本文件。
 */
#include "file_dialog.h"  // FileDialog 类声明
#include "util/logger.h"  // MM_LOG_* 日志宏

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN  // 减少 Windows 头文件体积
    #define NOMINMAX             // 避免 min/max 宏与 std 冲突
    #include <windows.h>         // Windows API
    #include <commdlg.h>         // GetOpenFileNameW 文件对话框
#endif

namespace melody_matrix::platform {  // 平台层命名空间

std::string FileDialog::openFile(const std::string& title,
                                  const std::string& filterDesc,
                                  const std::string& filterExt) {
    auto files = openFiles(title, filterDesc, filterExt);
    return files.empty() ? std::string() : files.front();
}

std::vector<std::string> FileDialog::openFiles(const std::string& title,
                                                const std::string& filterDesc,
                                                const std::string& filterExt) {
#ifdef _WIN32
    OPENFILENAMEW ofn = {};  // 文件对话框参数结构
    // 多选需要更大的缓冲区
    std::vector<wchar_t> fileBuf(65536, 0);  // 64KB 宽字符缓冲区

    std::wstring wTitle(title.begin(), title.end());  // 标题转宽字符
    std::wstring wFilterDesc(filterDesc.begin(), filterDesc.end());  // 过滤器描述
    std::wstring wFilterExt = L"*." + std::wstring(filterExt.begin(), filterExt.end());  // 扩展名模式
    std::vector<wchar_t> filterBuf;  // 过滤器缓冲区
    filterBuf.insert(filterBuf.end(), wFilterDesc.begin(), wFilterDesc.end());  // 写入描述
    filterBuf.push_back(L'\0');  // 描述终止符
    filterBuf.insert(filterBuf.end(), wFilterExt.begin(), wFilterExt.end());  // 写入模式
    filterBuf.push_back(L'\0');  // 模式终止符
    filterBuf.push_back(L'\0');  // 列表终止符

    ofn.lStructSize = sizeof(ofn);       // 结构体大小
    ofn.hwndOwner = nullptr;             // 无父窗口
    ofn.lpstrFilter = filterBuf.data();  // 过滤器
    ofn.lpstrFile = fileBuf.data();      // 多选结果缓冲区
    ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());  // 缓冲区大小
    ofn.lpstrTitle = wTitle.c_str();     // 对话框标题
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_ALLOWMULTISELECT | OFN_EXPLORER;  // Explorer 风格多选

    std::vector<std::string> results;  // UTF-8 路径结果列表

    if (GetOpenFileNameW(&ofn)) {  // 用户确认多选
        // 多选结果格式：目录\0文件1\0文件2\0\0
        wchar_t* ptr = fileBuf.data();  // 遍历指针
        std::wstring dir(ptr);          // 首段为目录或单文件完整路径
        ptr += dir.size() + 1;          // 跳到下一段

        if (*ptr == 0) {
            // 只选了一个文件 — dir 即为完整路径
            std::string path;  // UTF-8 路径
            int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);  // 计算长度
            if (len > 0) {
                path.resize(static_cast<size_t>(len - 1));  // 分配空间（不含 \0）
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, path.data(), len, nullptr, nullptr);  // 转换
            }
            results.push_back(path);  // 加入结果
        } else {
            // 多个文件：dir 为目录，后续为文件名
            while (*ptr != 0) {  // 遍历各文件名段
                std::wstring fileName(ptr);                    // 当前文件名
                std::wstring fullPath = dir + L"\\" + fileName;  // 拼接完整路径

                std::string path;  // UTF-8 路径
                int len = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);  // 计算长度
                if (len > 0) {
                    path.resize(static_cast<size_t>(len - 1));  // 分配空间
                    WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, path.data(), len, nullptr, nullptr);  // 转换
                }
                results.push_back(path);           // 加入结果
                ptr += fileName.size() + 1;        // 跳到下一段
            }
        }
    }

    return results;  // 返回路径列表（可能为空）
#else
    MM_LOG_WARN("FileDialog", "File dialog not implemented for this platform");  // 非 Windows 未实现
    return {};  // 返回空列表
#endif
}

} // namespace melody_matrix::platform

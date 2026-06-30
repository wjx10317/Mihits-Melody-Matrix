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
#include "file_dialog.h"
#include "util/logger.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <commdlg.h>
#endif

namespace melody_matrix::platform {

std::string FileDialog::openFile(const std::string& title,
                                  const std::string& filterDesc,
                                  const std::string& filterExt) {
#ifdef _WIN32
    // 构建 OPENFILENAMEW 结构
    OPENFILENAMEW ofn = {};
    wchar_t filePath[MAX_PATH] = {};

    std::wstring wTitle(title.begin(), title.end());

    // 过滤器格式："描述\0*.ext\0\0"
    std::wstring wFilterDesc(filterDesc.begin(), filterDesc.end());
    std::wstring wFilterExt = L"*." + std::wstring(filterExt.begin(), filterExt.end());
    std::vector<wchar_t> filterBuf;
    filterBuf.insert(filterBuf.end(), wFilterDesc.begin(), wFilterDesc.end());
    filterBuf.push_back(L'\0');
    filterBuf.insert(filterBuf.end(), wFilterExt.begin(), wFilterExt.end());
    filterBuf.push_back(L'\0');
    filterBuf.push_back(L'\0');

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr; // 无父窗口（可后续从 SDL 获取）
    ofn.lpstrFilter = filterBuf.data();
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wTitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        // 宽字符串转 UTF-8
        std::string result;
        int len = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            result.resize(static_cast<size_t>(len - 1)); // 去除末尾 \0
            WideCharToMultiByte(CP_UTF8, 0, filePath, -1, result.data(), len, nullptr, nullptr);
        }
        MM_LOG_INFO("FileDialog", "Selected file: %s", result.c_str());
        return result;
    }

    // 用户取消或对话框失败
    DWORD err = CommDlgExtendedError();
    if (err != 0) {
        MM_LOG_WARN("FileDialog", "GetOpenFileName failed with error: %lu", err);
    }
    return "";
#else
    // TODO: Linux/macOS — 需集成 tinyfiledialogs 或 zenity
    MM_LOG_WARN("FileDialog", "File dialog not implemented for this platform");
    return "";
#endif
}

std::vector<std::string> FileDialog::openFiles(const std::string& title,
                                                const std::string& filterDesc,
                                                const std::string& filterExt) {
#ifdef _WIN32
    OPENFILENAMEW ofn = {};
    // 多选需要更大的缓冲区
    std::vector<wchar_t> fileBuf(65536, 0);

    std::wstring wTitle(title.begin(), title.end());
    std::wstring wFilterDesc(filterDesc.begin(), filterDesc.end());
    std::wstring wFilterExt = L"*." + std::wstring(filterExt.begin(), filterExt.end());
    std::vector<wchar_t> filterBuf;
    filterBuf.insert(filterBuf.end(), wFilterDesc.begin(), wFilterDesc.end());
    filterBuf.push_back(L'\0');
    filterBuf.insert(filterBuf.end(), wFilterExt.begin(), wFilterExt.end());
    filterBuf.push_back(L'\0');
    filterBuf.push_back(L'\0');

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterBuf.data();
    ofn.lpstrFile = fileBuf.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
    ofn.lpstrTitle = wTitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT;

    std::vector<std::string> results;

    if (GetOpenFileNameW(&ofn)) {
        // 多选结果格式：目录\0文件1\0文件2\0\0
        wchar_t* ptr = fileBuf.data();
        std::wstring dir(ptr);
        ptr += dir.size() + 1;

        if (*ptr == 0) {
            // 只选了一个文件 — dir 即为完整路径
            std::string path;
            int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                path.resize(static_cast<size_t>(len - 1));
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, path.data(), len, nullptr, nullptr);
            }
            results.push_back(path);
        } else {
            // 多个文件：dir 为目录，后续为文件名
            while (*ptr != 0) {
                std::wstring fileName(ptr);
                std::wstring fullPath = dir + L"\\" + fileName;

                std::string path;
                int len = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    path.resize(static_cast<size_t>(len - 1));
                    WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, path.data(), len, nullptr, nullptr);
                }
                results.push_back(path);
                ptr += fileName.size() + 1;
            }
        }
    }

    return results;
#else
    MM_LOG_WARN("FileDialog", "File dialog not implemented for this platform");
    return {};
#endif
}

} // namespace melody_matrix::platform

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
    // 构建 OpenFileName 结构
    OPENFILENAMEW ofn = {};
    wchar_t filePath[MAX_PATH] = {};

    // 转换为宽字符串
    std::wstring wTitle(title.begin(), title.end());

    // 构建过滤器：描述\0扩展名\0\0
    // 格式: "osu! Beatmap Files\0*.osu\0\0"
    std::wstring wFilterDesc(filterDesc.begin(), filterDesc.end());
    std::wstring wFilterExt = L"*." + std::wstring(filterExt.begin(), filterExt.end());
    std::vector<wchar_t> filterBuf;
    filterBuf.insert(filterBuf.end(), wFilterDesc.begin(), wFilterDesc.end());
    filterBuf.push_back(L'\0');
    filterBuf.insert(filterBuf.end(), wFilterExt.begin(), wFilterExt.end());
    filterBuf.push_back(L'\0');
    filterBuf.push_back(L'\0');

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr; // 没有父窗口句柄（可后续从 SDL 获取）
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

    // 用户取消或失败
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
            // 只选了一个文件 — dir 就是完整路径
            std::string path;
            int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                path.resize(static_cast<size_t>(len - 1));
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, path.data(), len, nullptr, nullptr);
            }
            results.push_back(path);
        } else {
            // 多个文件
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

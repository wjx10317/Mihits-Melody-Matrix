/**
 * @file zip_extract.cpp
 * @brief ZIP 解压实现（Windows 多策略回退）
 *
 * 文件职责：
 *   实现 ZipExtract 类：tar.exe → PowerShell ZipFile 双策略解压，
 *   以及临时目录生命周期管理与 .osu 扫描。
 *
 * 主要依赖：
 *   zip_extract.h、util/logger.h、Windows API（CreateProcess）、<filesystem>。
 *
 * 在项目中的用法：
 *   由 song_select 等谱面导入模块调用，不直接暴露给 main。
 */
#include "zip_extract.h"
#include "util/logger.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <filesystem>
#include <random>

namespace melody_matrix::platform {

// ──────────────────────────────────────────────────────
//  辅助：生成随机后缀
// ──────────────────────────────────────────────────────

/**
 * @brief 生成随机字母数字后缀
 * @param length 后缀长度，默认 8
 * @return 随机字符串
 */
static std::string randomSuffix(size_t length = 8) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, sizeof(chars) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += chars[dist(rng)];
    }
    return result;
}

// ──────────────────────────────────────────────────────
//  辅助：执行命令并等待完成
// ──────────────────────────────────────────────────────

/**
 * @brief 启动隐藏子进程执行命令并等待退出
 * @param cmdLine 宽字符命令行
 * @param timeoutMs 等待超时（毫秒），默认 30 秒
 * @return 进程退出码；-1 表示 CreateProcess 失败
 */
static int runCommand(const std::wstring& cmdLine, DWORD timeoutMs = 30000) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // 重定向 stdout + stderr 到管道以便读取错误信息
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadOut, hWriteOut, hReadErr, hWriteErr;
    CreatePipe(&hReadOut, &hWriteOut, &sa, 0);
    CreatePipe(&hReadErr, &hWriteErr, &sa, 0);

    // 确保子进程不继承读端（否则管道无法检测 EOF）
    SetHandleInformation(hReadOut, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hReadErr, HANDLE_FLAG_INHERIT, 0);

    si.hStdOutput = hWriteOut;
    si.hStdError = hWriteErr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        buf.data(),
        nullptr, nullptr,
        TRUE,               // 继承句柄（管道写端）
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si, &pi
    );

    // 父进程关闭写端，子进程退出后读端才能收到 EOF
    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);

    if (!ok) {
        CloseHandle(hReadOut);
        CloseHandle(hReadErr);
        MM_LOG_ERROR("ZipExtract", "CreateProcess failed: %lu", GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, timeoutMs);

    // 读取 stderr（最多 4KB）用于诊断
    char errBuf[4096] = {};
    DWORD bytesRead = 0;
    ReadFile(hReadErr, errBuf, sizeof(errBuf) - 1, &bytesRead, nullptr);
    CloseHandle(hReadOut);
    CloseHandle(hReadErr);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (bytesRead > 0 && errBuf[0] != '\0') {
        // 去掉末尾换行
        while (bytesRead > 0 && (errBuf[bytesRead - 1] == '\n' || errBuf[bytesRead - 1] == '\r'))
            errBuf[--bytesRead] = '\0';
        MM_LOG_ERROR("ZipExtract", "Command stderr: %s", errBuf);
    }

    return static_cast<int>(exitCode);
}

// ──────────────────────────────────────────────────────
//  解压策略 1：tar.exe（Windows 10 1809+ 内置）
// ──────────────────────────────────────────────────────

/**
 * @brief 使用 tar.exe 解压 ZIP
 * @param zipPath ZIP 路径
 * @param destDir 目标目录
 * @return 退出码 0 为成功
 */
static bool extractWithTar(const std::string& zipPath, const std::string& destDir) {
#ifdef _WIN32
    // tar -xf <zip> -C <dest>；路径加引号以处理空格
    std::wstring wZip(zipPath.begin(), zipPath.end());
    std::wstring wDest(destDir.begin(), destDir.end());

    std::wstring cmd = L"tar.exe -xf \"" + wZip + L"\" -C \"" + wDest + L"\"";

    int ret = runCommand(cmd, 60000);  // tar 解压给 60 秒
    if (ret == 0) {
        MM_LOG_INFO("ZipExtract", "tar extraction succeeded");
        return true;
    }

    MM_LOG_WARN("ZipExtract", "tar extraction failed (exit code %d)", ret);
    return false;
#else
    (void)zipPath; (void)destDir;
    return false;
#endif
}

// ──────────────────────────────────────────────────────
//  解压策略 2：PowerShell .NET ZipFile
// ──────────────────────────────────────────────────────

/**
 * @brief 使用 PowerShell 调用 .NET ZipFile.ExtractToDirectory
 * @param zipPath ZIP 路径
 * @param destDir 目标目录
 * @return 成功 true
 */
static bool extractWithPowerShell(const std::string& zipPath, const std::string& destDir) {
#ifdef _WIN32
    std::wstring wZip(zipPath.begin(), zipPath.end());
    std::wstring wDest(destDir.begin(), destDir.end());

    // 单引号包裹路径，避免 PowerShell 变量展开
    std::wstring cmd =
        L"powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
        L"[System.IO.Compression.ZipFile]::ExtractToDirectory('" + wZip + L"','" + wDest + L"')"
        L"\"";

    int ret = runCommand(cmd, 60000);
    if (ret == 0) {
        MM_LOG_INFO("ZipExtract", "PowerShell extraction succeeded");
        return true;
    }

    MM_LOG_WARN("ZipExtract", "PowerShell extraction failed (exit code %d)", ret);
    return false;
#else
    (void)zipPath; (void)destDir;
    return false;
#endif
}

// ──────────────────────────────────────────────────────
//  ZipExtract 公共 API
// ──────────────────────────────────────────────────────

bool ZipExtract::extract(const std::string& zipPath, const std::string& destDir) {
    MM_LOG_INFO("ZipExtract", "Extracting: %s -> %s", zipPath.c_str(), destDir.c_str());

    // 0. 确保目标目录存在
    try {
        std::filesystem::create_directories(destDir);
    } catch (const std::exception& e) {
        MM_LOG_ERROR("ZipExtract", "Cannot create dest dir: %s", e.what());
        return false;
    }

    // 1. 优先 tar.exe（最快最可靠）
    if (extractWithTar(zipPath, destDir)) {
        return true;
    }

    // 2. 回退 PowerShell .NET ZipFile
    if (extractWithPowerShell(zipPath, destDir)) {
        return true;
    }

    // 3. 容错：部分工具非零退出但实际已解压成功
    try {
        if (std::filesystem::exists(destDir) && !std::filesystem::is_empty(destDir)) {
            MM_LOG_WARN("ZipExtract", "All tools reported failure but dest dir has content, accepting");
            return true;
        }
    } catch (...) {}

    MM_LOG_ERROR("ZipExtract", "All extraction methods failed");
    return false;
}

std::string ZipExtract::createTempDir() {
    std::filesystem::path tempBase = std::filesystem::temp_directory_path();
    std::string suffix = randomSuffix();
    std::filesystem::path tempDir = tempBase / ("melody_matrix_import_" + suffix);

    try {
        std::filesystem::create_directories(tempDir);
    } catch (const std::exception& e) {
        MM_LOG_ERROR("ZipExtract", "Cannot create temp dir: %s", e.what());
        return "";
    }

    MM_LOG_INFO("ZipExtract", "Created temp dir: %s", tempDir.string().c_str());
    return tempDir.string();
}

void ZipExtract::removeDir(const std::string& dirPath) {
    try {
        if (std::filesystem::exists(dirPath)) {
            std::filesystem::remove_all(dirPath);
            MM_LOG_INFO("ZipExtract", "Removed temp dir: %s", dirPath.c_str());
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("ZipExtract", "Failed to remove temp dir: %s", e.what());
    }
}

std::vector<std::string> ZipExtract::findOsuFiles(const std::string& dirPath) {
    std::vector<std::string> results;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(tolower(c));
            if (ext == ".osu") {
                results.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("ZipExtract", "Error scanning for .osu files: %s", e.what());
    }

    // 按文件名排序（不同难度通常按字母序排列）
    std::sort(results.begin(), results.end());
    return results;
}

} // namespace melody_matrix::platform

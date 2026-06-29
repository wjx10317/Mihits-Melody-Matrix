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
//  辅助：执行命令并等待完成（返回退出码，-1 表示创建进程失败）
// ──────────────────────────────────────────────────────
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

    // 确保子进程不继承读端
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
        TRUE,               // 继承句柄
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si, &pi
    );

    // 关闭父进程的写端
    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);

    if (!ok) {
        CloseHandle(hReadOut);
        CloseHandle(hReadErr);
        MM_LOG_ERROR("ZipExtract", "CreateProcess failed: %lu", GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, timeoutMs);

    // 读取 stderr（最多 4KB）
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
//  解压 ZIP — 策略1: tar.exe（Windows 10 内置）
// ──────────────────────────────────────────────────────
static bool extractWithTar(const std::string& zipPath, const std::string& destDir) {
#ifdef _WIN32
    // tar -xf <zip> -C <dest>   （tar.exe 在 Windows 10 1809+ 中可用）
    // 注意：tar 路径用引号包裹以处理空格和特殊字符
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
//  解压 ZIP — 策略2: PowerShell .NET ZipFile（比 Expand-Archive 更可靠）
// ──────────────────────────────────────────────────────
static bool extractWithPowerShell(const std::string& zipPath, const std::string& destDir) {
#ifdef _WIN32
    std::wstring wZip(zipPath.begin(), zipPath.end());
    std::wstring wDest(destDir.begin(), destDir.end());

    // 使用 .NET 的 System.IO.Compression.ZipFile 类，比 Expand-Archive 更轻量可靠
    // 用单引号包裹路径避免变量展开问题
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
//  解压 ZIP（主入口）
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

    // 1. 尝试策略1: tar.exe（最快最可靠）
    if (extractWithTar(zipPath, destDir)) {
        return true;
    }

    // 2. 尝试策略2: PowerShell .NET ZipFile
    if (extractWithPowerShell(zipPath, destDir)) {
        return true;
    }

    // 3. 验证：即使返回非零码，检查目录是否有内容（有些工具非零但实际成功）
    try {
        if (std::filesystem::exists(destDir) && !std::filesystem::is_empty(destDir)) {
            MM_LOG_WARN("ZipExtract", "All tools reported failure but dest dir has content, accepting");
            return true;
        }
    } catch (...) {}

    MM_LOG_ERROR("ZipExtract", "All extraction methods failed");
    return false;
}

// ──────────────────────────────────────────────────────
//  临时目录
// ──────────────────────────────────────────────────────
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

// ──────────────────────────────────────────────────────
//  清理临时目录
// ──────────────────────────────────────────────────────
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

// ──────────────────────────────────────────────────────
//  查找 .osu 文件
// ──────────────────────────────────────────────────────
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

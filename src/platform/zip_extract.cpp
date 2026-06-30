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
#include "zip_extract.h"  // ZipExtract 类声明
#include "util/logger.h"  // MM_LOG_* 日志宏

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN  // 减少 Windows 头文件体积
    #define NOMINMAX             // 避免 min/max 宏与 std 冲突
    #include <windows.h>         // CreateProcess、管道等 API
#endif

#include <filesystem>  // 目录创建、递归遍历
#include <random>      // 随机后缀生成

namespace melody_matrix::platform {  // 平台层命名空间

// ──────────────────────────────────────────────────────
//  辅助：生成随机后缀
// ──────────────────────────────────────────────────────

/**
 * @brief 生成随机字母数字后缀
 * @param length 后缀长度，默认 8
 * @return 随机字符串
 */
static std::string randomSuffix(size_t length = 8) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";  // 可用字符集
    std::mt19937 rng(std::random_device{}());  // Mersenne Twister 随机数引擎
    std::uniform_int_distribution<size_t> dist(0, sizeof(chars) - 2);  // 均匀分布（排除末尾 \0）
    std::string result;           // 结果字符串
    result.reserve(length);       // 预分配容量
    for (size_t i = 0; i < length; ++i) {
        result += chars[dist(rng)];  // 追加随机字符
    }
    return result;  // 返回随机后缀
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
    STARTUPINFOW si = {};  // 进程启动信息
    si.cb = sizeof(si);    // 结构体大小
    si.dwFlags = STARTF_USESHOWWINDOW;  // 使用 wShowWindow 字段
    si.wShowWindow = SW_HIDE;           // 隐藏子进程窗口

    // 重定向 stdout + stderr 到管道以便读取错误信息
    SECURITY_ATTRIBUTES sa = {};  // 安全属性（可继承句柄）
    sa.nLength = sizeof(sa);      // 结构体大小
    sa.bInheritHandle = TRUE;     // 句柄可被继承

    HANDLE hReadOut, hWriteOut, hReadErr, hWriteErr;  // stdout/stderr 管道读写端
    CreatePipe(&hReadOut, &hWriteOut, &sa, 0);  // 创建 stdout 管道
    CreatePipe(&hReadErr, &hWriteErr, &sa, 0);  // 创建 stderr 管道

    // 确保子进程不继承读端（否则管道无法检测 EOF）
    SetHandleInformation(hReadOut, HANDLE_FLAG_INHERIT, 0);  // 读端不继承
    SetHandleInformation(hReadErr, HANDLE_FLAG_INHERIT, 0);  // 读端不继承

    si.hStdOutput = hWriteOut;  // 子进程 stdout 写端
    si.hStdError = hWriteErr;   // 子进程 stderr 写端
    si.dwFlags |= STARTF_USESTDHANDLES;  // 启用标准句柄重定向

    PROCESS_INFORMATION pi = {};  // 进程信息（PID/句柄）

    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());  // 命令行转可写缓冲区
    buf.push_back(L'\0');  // 空终止

    BOOL ok = CreateProcessW(
        nullptr,        // 不指定可执行文件路径（命令行含程序名）
        buf.data(),     // 命令行字符串
        nullptr, nullptr,  // 默认进程/线程安全属性
        TRUE,               // 继承句柄（管道写端）
        CREATE_NO_WINDOW,   // 不创建控制台窗口
        nullptr,            // 默认环境块
        nullptr,            // 默认工作目录
        &si, &pi            // 启动信息与进程信息
    );

    // 父进程关闭写端，子进程退出后读端才能收到 EOF
    CloseHandle(hWriteOut);  // 关闭 stdout 写端
    CloseHandle(hWriteErr);  // 关闭 stderr 写端

    if (!ok) {
        CloseHandle(hReadOut);  // 清理 stdout 读端
        CloseHandle(hReadErr);  // 清理 stderr 读端
        MM_LOG_ERROR("ZipExtract", "CreateProcess failed: %lu", GetLastError());  // 记录错误码
        return -1;  // 创建进程失败
    }

    WaitForSingleObject(pi.hProcess, timeoutMs);  // 等待子进程结束（带超时）

    // 读取 stderr（最多 4KB）用于诊断
    char errBuf[4096] = {};  // stderr 缓冲区
    DWORD bytesRead = 0;     // 实际读取字节数
    ReadFile(hReadErr, errBuf, sizeof(errBuf) - 1, &bytesRead, nullptr);  // 读取 stderr
    CloseHandle(hReadOut);  // 关闭 stdout 读端
    CloseHandle(hReadErr);  // 关闭 stderr 读端

    DWORD exitCode = 1;  // 默认退出码
    GetExitCodeProcess(pi.hProcess, &exitCode);  // 获取进程退出码

    CloseHandle(pi.hThread);   // 关闭线程句柄
    CloseHandle(pi.hProcess);  // 关闭进程句柄

    if (bytesRead > 0 && errBuf[0] != '\0') {
        // 去掉末尾换行
        while (bytesRead > 0 && (errBuf[bytesRead - 1] == '\n' || errBuf[bytesRead - 1] == '\r'))
            errBuf[--bytesRead] = '\0';  // 截断尾部换行符
        MM_LOG_ERROR("ZipExtract", "Command stderr: %s", errBuf);  // 记录 stderr 内容
    }

    return static_cast<int>(exitCode);  // 返回退出码
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
    std::wstring wZip(zipPath.begin(), zipPath.end());    // ZIP 路径转宽字符
    std::wstring wDest(destDir.begin(), destDir.end());    // 目标目录转宽字符

    std::wstring cmd = L"tar.exe -xf \"" + wZip + L"\" -C \"" + wDest + L"\"";  // 组装 tar 命令

    int ret = runCommand(cmd, 60000);  // tar 解压给 60 秒
    if (ret == 0) {
        MM_LOG_INFO("ZipExtract", "tar extraction succeeded");  // 解压成功
        return true;  // 返回成功
    }

    MM_LOG_WARN("ZipExtract", "tar extraction failed (exit code %d)", ret);  // 记录失败退出码
    return false;  // 返回失败
#else
    (void)zipPath; (void)destDir;  // 非 Windows 平台抑制未使用警告
    return false;  // 非 Windows 不支持
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
    std::wstring wZip(zipPath.begin(), zipPath.end());    // ZIP 路径转宽字符
    std::wstring wDest(destDir.begin(), destDir.end());    // 目标目录转宽字符

    // 单引号包裹路径，避免 PowerShell 变量展开
    std::wstring cmd =
        L"powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
        L"[System.IO.Compression.ZipFile]::ExtractToDirectory('" + wZip + L"','" + wDest + L"')"
        L"\"";  // 组装 PowerShell 解压命令

    int ret = runCommand(cmd, 60000);  // 执行命令，超时 60 秒
    if (ret == 0) {
        MM_LOG_INFO("ZipExtract", "PowerShell extraction succeeded");  // 解压成功
        return true;  // 返回成功
    }

    MM_LOG_WARN("ZipExtract", "PowerShell extraction failed (exit code %d)", ret);  // 记录失败
    return false;  // 返回失败
#else
    (void)zipPath; (void)destDir;  // 非 Windows 平台抑制未使用警告
    return false;  // 非 Windows 不支持
#endif
}

// ──────────────────────────────────────────────────────
//  ZipExtract 公共 API
// ──────────────────────────────────────────────────────

bool ZipExtract::extract(const std::string& zipPath, const std::string& destDir) {
    MM_LOG_INFO("ZipExtract", "Extracting: %s -> %s", zipPath.c_str(), destDir.c_str());  // 记录解压任务

    // 0. 确保目标目录存在
    try {
        std::filesystem::create_directories(destDir);  // 递归创建目标目录
    } catch (const std::exception& e) {
        MM_LOG_ERROR("ZipExtract", "Cannot create dest dir: %s", e.what());  // 创建失败
        return false;  // 无法继续解压
    }

    // 1. 优先 tar.exe（最快最可靠）
    if (extractWithTar(zipPath, destDir)) {
        return true;  // tar 成功则直接返回
    }

    // 2. 回退 PowerShell .NET ZipFile
    if (extractWithPowerShell(zipPath, destDir)) {
        return true;  // PowerShell 成功则返回
    }

    // 3. 容错：部分工具非零退出但实际已解压成功
    try {
        if (std::filesystem::exists(destDir) && !std::filesystem::is_empty(destDir)) {
            MM_LOG_WARN("ZipExtract", "All tools reported failure but dest dir has content, accepting");  // 容错接受
            return true;  // 目录非空则视为成功
        }
    } catch (...) {}  // 检查过程异常则忽略

    MM_LOG_ERROR("ZipExtract", "All extraction methods failed");  // 全部策略失败
    return false;  // 返回失败
}

std::string ZipExtract::createTempDir() {
    std::filesystem::path tempBase = std::filesystem::temp_directory_path();  // 系统临时目录
    std::string suffix = randomSuffix();  // 生成随机后缀
    std::filesystem::path tempDir = tempBase / ("melody_matrix_import_" + suffix);  // 拼接临时目录路径

    try {
        std::filesystem::create_directories(tempDir);  // 创建目录
    } catch (const std::exception& e) {
        MM_LOG_ERROR("ZipExtract", "Cannot create temp dir: %s", e.what());  // 创建失败
        return "";  // 返回空串表示失败
    }

    MM_LOG_INFO("ZipExtract", "Created temp dir: %s", tempDir.string().c_str());  // 记录路径
    return tempDir.string();  // 返回临时目录路径
}

void ZipExtract::removeDir(const std::string& dirPath) {
    try {
        if (std::filesystem::exists(dirPath)) {  // 目录存在才删除
            std::filesystem::remove_all(dirPath);  // 递归删除
            MM_LOG_INFO("ZipExtract", "Removed temp dir: %s", dirPath.c_str());  // 记录删除成功
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("ZipExtract", "Failed to remove temp dir: %s", e.what());  // 删除失败仅警告
    }
}

std::vector<std::string> ZipExtract::findOsuFiles(const std::string& dirPath) {
    std::vector<std::string> results;  // 找到的 .osu 路径列表

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {  // 递归遍历
            if (!entry.is_regular_file()) continue;  // 跳过非普通文件
            std::string ext = entry.path().extension().string();  // 获取扩展名
            for (auto& c : ext) c = static_cast<char>(tolower(c));  // 扩展名转小写
            if (ext == ".osu") {
                results.push_back(entry.path().string());  // 匹配 .osu 则加入列表
            }
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("ZipExtract", "Error scanning for .osu files: %s", e.what());  // 扫描异常
    }

    // 按文件名排序（不同难度通常按字母序排列）
    std::sort(results.begin(), results.end());  // 字典序排序
    return results;  // 返回路径列表
}

} // namespace melody_matrix::platform

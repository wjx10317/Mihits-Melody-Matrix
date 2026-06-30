/**
 * @file main.cpp
 * @brief Melody Matrix 程序入口
 *
 * 文件职责：
 *   初始化日志与工作目录，创建 Kernel 单例，注册全部游戏状态并驱动主循环。
 *
 * 主要依赖：
 *   core/kernel.h、core/states/*、util/logger、platform/config、platform/file_system、SDL。
 *
 * 在项目中的用法：
 *   可执行文件启动后直接进入 main()；异常时记录 FATAL 日志并以非零码退出。
 */
#include "core/kernel.h"                      // 游戏内核：窗口、主循环、状态机
#include "core/states/boot_state.h"           // 启动加载状态
#include "core/states/main_menu_state.h"      // 主菜单状态
#include "core/states/song_select_state.h"  // 选曲状态
#include "core/states/playing_state.h"        // 游玩状态
#include "core/states/paused_state.h"         // 暂停状态
#include "core/states/result_state.h"         // 结算状态
#include "util/logger.h"                      // 日志系统
#include "util/exceptions.h"                  // 致命异常类型
#include "platform/config.h"                  // INI 配置管理
#include "platform/file_system.h"             // 文件系统工具（含临时目录清理）

#include <SDL.h>          // SDL 初始化与跨平台抽象
#include <SDL_main.h>     // Windows 下正确的 main 入口宏
#include <filesystem>     // 路径解析与工作目录切换
#include <iostream>       // std::cerr 异常信息输出
#include <exception>      // std::exception 捕获

/**
 * @brief 程序入口
 * @param argc 参数个数（未使用）
 * @param argv 参数列表；argv[0] 用于定位可执行文件目录
 * @return 0 正常退出，1 初始化或致命错误
 */
int main(int /*argc*/, char* argv[]) {  // 程序入口；argc 未使用
    using namespace melody_matrix;      // 使用项目根命名空间

    // ── 将工作目录固定为可执行文件所在目录 ──
    // 确保 assets/beatmaps 等相对路径始终相对于 exe 解析，
    // 不依赖 IDE/快捷方式启动时的 CWD
    {
        std::string exeDir = std::filesystem::path(argv[0]).parent_path().string();  // 从 argv[0] 提取 exe 所在目录
        if (!exeDir.empty()) {                                                         // 目录非空时才切换
            std::filesystem::current_path(exeDir);                                     // 将进程 CWD 设为 exe 目录
        }
    }

    // ── 初始化日志器 ──
    util::Logger::init("logs/melody_matrix.log", util::Level::DEBUG);  // 创建日志文件，最低级别 DEBUG
    MM_LOG_INFO("Main", "Melody Matrix starting...");                  // 记录启动信息

    try {
        auto& kernel = core::Kernel::instance();  // 获取 Kernel 单例引用

        // ── 初始化内核（窗口 + GL 上下文 + ImGui）──
        // 分辨率在 Kernel::init() 内部从 config.ini 加载
        if (!kernel.init("Melody Matrix")) {           // 初始化 SDL/GL/ImGui；失败则退出
            MM_LOG_FATAL("Main", "Kernel initialization failed");  // 记录致命错误
            util::Logger::shutdown();                  // 关闭日志文件
            return 1;                                    // 以错误码退出
        }

        // ── 注册所有游戏状态实例 ──
        auto& sm = kernel.stateManager();  // 获取状态机管理器引用
        sm.registerState(core::GameState::Boot, std::make_unique<core::BootState>());               // 注册启动状态
        sm.registerState(core::GameState::MainMenu, std::make_unique<core::MainMenuState>());       // 注册主菜单
        sm.registerState(core::GameState::SongSelect, std::make_unique<core::SongSelectState>());   // 注册选曲
        sm.registerState(core::GameState::Playing, std::make_unique<core::PlayingState>());         // 注册游玩
        sm.registerState(core::GameState::Paused, std::make_unique<core::PausedState>());           // 注册暂停
        sm.registerState(core::GameState::Result, std::make_unique<core::ResultState>());           // 注册结算

        // ── 进入 Boot 启动流程 ──
        sm.transitionTo(core::GameState::Boot);  // 切换到 Boot 状态开始加载

        // ── 阻塞运行主循环直到 requestExit ──
        kernel.run();  // 进入主循环（事件、更新、渲染）

        kernel.shutdown();  // 正常退出时释放内核资源
    } catch (const util::FatalException& e) {  // 捕获项目定义的致命异常
        MM_LOG_FATAL("Main", std::string("Fatal: ") + e.what());  // 写入 FATAL 日志
        std::cerr << "Fatal error: " << e.what() << std::endl;    // 控制台输出
        return 1;  // 以错误码退出
    } catch (const std::exception& e) {  // 捕获标准库异常
        MM_LOG_FATAL("Main", std::string("Unhandled exception: ") + e.what());  // 写入 FATAL 日志
        std::cerr << "Unhandled exception: " << e.what() << std::endl;        // 控制台输出
        return 1;  // 以错误码退出
    } catch (...) {  // 捕获未知异常
        MM_LOG_FATAL("Main", "Unknown exception");              // 写入 FATAL 日志
        std::cerr << "Unknown fatal error" << std::endl;        // 控制台输出
        return 1;  // 以错误码退出
    }

    // ── 清理临时文件并关闭日志 ──
    platform::FileSystem::cleanupTemp();  // 删除 temp 临时目录
    util::Logger::shutdown();             // 刷新并关闭日志文件
    return 0;                             // 正常退出
}

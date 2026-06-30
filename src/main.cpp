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
#include "core/kernel.h"
#include "core/states/boot_state.h"
#include "core/states/main_menu_state.h"
#include "core/states/song_select_state.h"
#include "core/states/playing_state.h"
#include "core/states/paused_state.h"
#include "core/states/result_state.h"
#include "util/logger.h"
#include "util/exceptions.h"
#include "platform/config.h"
#include "platform/file_system.h"

#include <SDL.h>
#include <SDL_main.h>
#include <filesystem>
#include <iostream>
#include <exception>

/**
 * @brief 程序入口
 * @param argc 参数个数（未使用）
 * @param argv 参数列表；argv[0] 用于定位可执行文件目录
 * @return 0 正常退出，1 初始化或致命错误
 */
int main(int /*argc*/, char* argv[]) {
    using namespace melody_matrix;

    // ── 将工作目录固定为可执行文件所在目录 ──
    // 确保 assets/beatmaps 等相对路径始终相对于 exe 解析，
    // 不依赖 IDE/快捷方式启动时的 CWD
    {
        std::string exeDir = std::filesystem::path(argv[0]).parent_path().string();
        if (!exeDir.empty()) {
            std::filesystem::current_path(exeDir);
        }
    }

    // ── 初始化日志器 ──
    util::Logger::init("logs/melody_matrix.log", util::Level::DEBUG);
    MM_LOG_INFO("Main", "Melody Matrix starting...");

    try {
        auto& kernel = core::Kernel::instance();

        // ── 初始化内核（窗口 + GL 上下文 + ImGui）──
        // 分辨率在 Kernel::init() 内部从 config.ini 加载
        if (!kernel.init("Melody Matrix")) {
            MM_LOG_FATAL("Main", "Kernel initialization failed");
            util::Logger::shutdown();
            return 1;
        }

        // ── 注册所有游戏状态实例 ──
        auto& sm = kernel.stateManager();
        sm.registerState(core::GameState::Boot, std::make_unique<core::BootState>());
        sm.registerState(core::GameState::MainMenu, std::make_unique<core::MainMenuState>());
        sm.registerState(core::GameState::SongSelect, std::make_unique<core::SongSelectState>());
        sm.registerState(core::GameState::Playing, std::make_unique<core::PlayingState>());
        sm.registerState(core::GameState::Paused, std::make_unique<core::PausedState>());
        sm.registerState(core::GameState::Result, std::make_unique<core::ResultState>());

        // ── 进入 Boot 启动流程 ──
        sm.transitionTo(core::GameState::Boot);

        // ── 阻塞运行主循环直到 requestExit ──
        kernel.run();

        kernel.shutdown();
    } catch (const util::FatalException& e) {
        MM_LOG_FATAL("Main", std::string("Fatal: ") + e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        MM_LOG_FATAL("Main", std::string("Unhandled exception: ") + e.what());
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        MM_LOG_FATAL("Main", "Unknown exception");
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }

    // ── 清理临时文件并关闭日志 ──
    platform::FileSystem::cleanupTemp();
    util::Logger::shutdown();
    return 0;
}

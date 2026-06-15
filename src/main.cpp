/// Melody Matrix — 一款节奏游戏
/// 入口点：初始化 Kernel 并运行主循环。

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
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    using namespace melody_matrix;

    // ── 初始化日志器 ──
    util::Logger::init("logs/melody_matrix.log", util::Level::DEBUG);
    MM_LOG_INFO("Main", "Melody Matrix starting...");

    try {
        auto& kernel = core::Kernel::instance();

        // ── 初始化内核（窗口 + GL 上下文 + ImGui）──
        // 分辨率在 Kernel::init() 内部从配置加载
        if (!kernel.init("Melody Matrix")) {
            MM_LOG_FATAL("Main", "Kernel initialization failed");
            util::Logger::shutdown();
            return 1;
        }

        // ── 注册游戏状态 ──
        auto& sm = kernel.stateManager();
        sm.registerState(core::GameState::Boot, std::make_unique<core::BootState>());
        sm.registerState(core::GameState::MainMenu, std::make_unique<core::MainMenuState>());
        sm.registerState(core::GameState::SongSelect, std::make_unique<core::SongSelectState>());
        sm.registerState(core::GameState::Playing, std::make_unique<core::PlayingState>());
        sm.registerState(core::GameState::Paused, std::make_unique<core::PausedState>());
        sm.registerState(core::GameState::Result, std::make_unique<core::ResultState>());

        // ── 进入初始状态 ──
        sm.transitionTo(core::GameState::Boot);

        // ── 运行主循环 ──
        kernel.run();

        // ── 清理 ──
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

    // ── 清理临时文件并关闭 ──
    platform::FileSystem::cleanupTemp();
    util::Logger::shutdown();
    return 0;
}

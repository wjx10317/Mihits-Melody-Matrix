/**
 * @file verify_gameplay_input.cpp
 * @brief 游玩输入适配器自测：Raw 优先、失败回退 SDL、SendInput 触发 sink
 *
 * 用法：从 build 输出目录运行 verify_gameplay_input.exe
 * 退出码 0 = 全部通过。
 */
#include "input/gameplay_input.h"
#include "util/logger.h"

#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

using melody_matrix::input::createGameplayInput;
using melody_matrix::input::GameplayKeySink;
using melody_matrix::input::IGameplayInput;

static int g_failures = 0;

static void expect(bool ok, const char* msg) {
    if (ok) {
        std::printf("[PASS] %s\n", msg);
    } else {
        std::printf("[FAIL] %s\n", msg);
        ++g_failures;
    }
}

#ifdef _WIN32
static void sendKeyVk(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}
#endif

int main(int /*argc*/, char* argv[]) {
    if (argv[0] && argv[0][0]) {
        // CWD 对齐 exe，便于写 logs/
        // （与主程序一致，避免相对路径漂移）
    }

    melody_matrix::util::Logger::init("logs/verify_gameplay_input.log",
                                      melody_matrix::util::Level::INFO);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // ── Case 1: null window → Raw init 失败 → 回退 SDL ──
    {
        auto adapter = createGameplayInput(nullptr);
        expect(adapter != nullptr, "fallback adapter non-null");
        expect(adapter && std::string(adapter->name()) == "sdl",
               "null window falls back to backend=sdl");
        if (adapter) adapter->shutdown();
    }

    // ── Case 2: 真实窗口 → Win 上应为 raw ──
    SDL_Window* window = SDL_CreateWindow(
        "verify_gameplay_input",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        320, 240, SDL_WINDOW_SHOWN);
    expect(window != nullptr, "SDL_CreateWindow");
    if (!window) {
        SDL_Quit();
        return 1;
    }

#ifdef _WIN32
    SDL_RaiseWindow(window);
    SDL_Delay(100);
#endif

    auto adapter = createGameplayInput(window);
    expect(adapter != nullptr, "createGameplayInput with window");
#ifdef _WIN32
    expect(adapter && std::string(adapter->name()) == "raw",
           "Windows prefers backend=raw");
#else
    expect(adapter && std::string(adapter->name()) == "sdl",
           "non-Windows uses backend=sdl");
#endif

    // ── Case 3: SendInput 触发判定 sink（Raw 或 SDL）──
    int pressCount = 0;
    int releaseCount = 0;
    int32_t lastKey = 0;
    const GameplayKeySink sink = [&](int32_t sdlKey, bool pressed, int64_t /*hostMs*/) {
        lastKey = sdlKey;
        if (pressed) ++pressCount;
        else ++releaseCount;
    };

#ifdef _WIN32
    // 确保窗口前台
    {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) == SDL_TRUE) {
            SetForegroundWindow(info.info.win.window);
            SetFocus(info.info.win.window);
        }
    }
    SDL_Delay(50);

    sendKeyVk('D', true);
    sendKeyVk('D', false);

    const Uint32 deadline = SDL_GetTicks() + 1000;
    while (SDL_GetTicks() < deadline && (pressCount < 1 || releaseCount < 1)) {
        adapter->poll(sink);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            adapter->onSdlEvent(e, sink);
        }
        SDL_Delay(1);
    }

    expect(pressCount >= 1, "received at least one key press via adapter");
    expect(releaseCount >= 1, "received at least one key release via adapter");
    if (pressCount + releaseCount > 0) {
        expect(lastKey == SDLK_d, "mapped key is SDLK_d ('d')");
    }

    // 连发抑制：按住不应多次 press（Raw 路径）
    if (adapter && std::string(adapter->name()) == "raw") {
        pressCount = 0;
        releaseCount = 0;
        sendKeyVk('F', true);
        // 泵一段时间，模拟可能的 repeat
        const Uint32 holdEnd = SDL_GetTicks() + 200;
        while (SDL_GetTicks() < holdEnd) {
            adapter->poll(sink);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                adapter->onSdlEvent(e, sink);
            }
            SDL_Delay(1);
        }
        sendKeyVk('F', false);
        const Uint32 upEnd = SDL_GetTicks() + 200;
        while (SDL_GetTicks() < upEnd) {
            adapter->poll(sink);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                adapter->onSdlEvent(e, sink);
            }
            SDL_Delay(1);
        }
        expect(pressCount == 1, "Raw suppresses auto-repeat (single press while held)");
        expect(releaseCount == 1, "Raw emits single release");
    }
#else
    (void)sink;
    std::printf("[SKIP] SendInput key path (non-Windows)\n");
#endif

    if (adapter) {
        adapter->shutdown();
        adapter.reset();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    melody_matrix::util::Logger::shutdown();

    std::printf("\n%s (%d failure(s))\n",
                g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

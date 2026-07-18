/**
 * @file gameplay_input_factory.cpp
 * @brief 创建游玩输入适配器：优先 Raw，失败回退 SDL
 */
#include "input/gameplay_input.h"
#include "util/logger.h"

namespace melody_matrix::input {

std::unique_ptr<IGameplayInput> makeSdlPollInputAdapter();
std::unique_ptr<IGameplayInput> tryMakeRawInputAdapter();

std::unique_ptr<IGameplayInput> createGameplayInput(SDL_Window* window) {
    if (auto raw = tryMakeRawInputAdapter()) {
        if (raw->init(window)) {
            MM_LOG_INFO("Input", std::string("Gameplay input backend=") + raw->name());
            return raw;
        }
        MM_LOG_WARN("Input", "Raw input init failed; falling back to SDL poll");
        raw->shutdown();
        raw.reset();
    }

    auto sdl = makeSdlPollInputAdapter();
    if (!sdl->init(window)) {
        MM_LOG_WARN("Input", "SDL poll input init failed (unexpected)");
    }
    MM_LOG_INFO("Input", std::string("Gameplay input backend=") + sdl->name());
    return sdl;
}

} // namespace melody_matrix::input

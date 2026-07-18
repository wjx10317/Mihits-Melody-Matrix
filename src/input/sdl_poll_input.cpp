/**
 * @file sdl_poll_input.cpp
 * @brief SDL_PollEvent 游玩按键适配器（回退路径）
 *
 * 在 KEYDOWN/UP 处理瞬间取样 HostClock；行为对齐历史 Kernel 路径。
 */
#include "input/gameplay_input.h"
#include "time/host_clock.h"

#include <SDL.h>

namespace melody_matrix::input {
namespace {

class SdlPollInputAdapter final : public IGameplayInput {
public:
    bool init(SDL_Window* /*window*/) override { return true; }

    void shutdown() override {}

    void onSdlEvent(const SDL_Event& event, const GameplayKeySink& sink) override {
        if (!sink) return;
        if (event.type == SDL_KEYDOWN) {
            if (event.key.repeat) return;
            sink(static_cast<int32_t>(event.key.keysym.sym), true, time::HostClock::nowMs());
        } else if (event.type == SDL_KEYUP) {
            sink(static_cast<int32_t>(event.key.keysym.sym), false, time::HostClock::nowMs());
        }
    }

    const char* name() const override { return "sdl"; }
};

} // namespace

std::unique_ptr<IGameplayInput> makeSdlPollInputAdapter() {
    return std::make_unique<SdlPollInputAdapter>();
}

} // namespace melody_matrix::input

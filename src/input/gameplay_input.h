/**
 * @file gameplay_input.h
 * @brief 游玩判定按键适配器接口（Raw / SDL 二选一）
 *
 * Raw 与 SDL_PollEvent 为可替换适配器：初始化优先 Raw，失败回退 SDL。
 * Playing 判定只从当前适配器取样 HostClock，经统一出口换算歌曲时间。
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

struct SDL_Window;
union SDL_Event;

namespace melody_matrix::input {

/// 适配器产出：SDL 键码 + 按下/释放 + HostClock 毫秒戳
using GameplayKeySink = std::function<void(int32_t sdlKey, bool pressed, int64_t hostMs)>;

/**
 * @brief 游玩按键输入适配器
 *
 * 负责「何时取样 HostClock」与「产出 key/pressed」。
 * Kernel 持有唯一 active 实例；ImGui/菜单仍可走 SDL 事件泵。
 */
class IGameplayInput {
public:
    virtual ~IGameplayInput() = default;

    /// 注册平台资源；失败则调用方改用 SDL 适配器
    virtual bool init(SDL_Window* window) = 0;
    virtual void shutdown() = 0;

    /// 每个 SDL 事件转交；适配器自行决定是否产出判定键 / 诊断
    virtual void onSdlEvent(const SDL_Event& event, const GameplayKeySink& sink) = 0;

    /// 冲刷适配器内部队列（Raw 在 WndProc 入队后由此交付）
    virtual void poll(const GameplayKeySink& sink) { (void)sink; }

    /// "raw" 或 "sdl"，供日志
    virtual const char* name() const = 0;
};

/**
 * @brief 创建游玩输入适配器：优先 Raw，失败或非 Win 则 SDL
 *
 * 成功后已 init；调用方负责 shutdown（或析构前调用）。
 */
std::unique_ptr<IGameplayInput> createGameplayInput(SDL_Window* window);

} // namespace melody_matrix::input

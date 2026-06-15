#pragma once

#include <SDL.h>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace melody_matrix::input {

/// 按键事件数据
struct KeyEvent {
    int64_t timestamp;   ///< 按键按下时的游戏时间（来自音频时钟）
    int32_t keyCode;     ///< SDL 按键代码
    bool    pressed;     ///< true = 按键按下, false = 按键释放
};

/// 输入管理器 — 将 SDL 事件转换为语义游戏事件。
/// 将 SDL 按键代码绑定到游戏动作并提供按键状态查询。
class InputManager {
public:
    InputManager() = default;

    /// 处理 SDL 事件。如果事件被消费返回 true。
    bool processEvent(const SDL_Event& event, int64_t audioTimeMs);

    /// 检查按键当前是否被按住
    bool isKeyDown(int32_t keyCode) const;

    /// 重置按键状态（例如在状态转换时）
    void reset();

    /// 按键按下回调（供 JudgeQueue 使用）
    std::function<void(const KeyEvent&)> onKeyPress;

    /// 按键释放回调
    std::function<void(const KeyEvent&)> onKeyRelease;

private:
    std::unordered_map<int32_t, bool> m_keyStates;
};

} // namespace input

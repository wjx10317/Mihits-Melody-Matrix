#pragma once

#include "game_state.h"

namespace melody_matrix::core {

/// 所有游戏状态的抽象基类。
/// 每个状态拥有自己的更新/渲染逻辑，并可以通过 update() 的返回值请求状态转换。
class GameStateBase {
public:
    virtual ~GameStateBase() = default;

    /// 当此状态变为活动状态时调用
    virtual void onEnter() = 0;

    /// 当此状态被替换时调用
    virtual void onExit() = 0;

    /// 固定步长逻辑更新（以 240 Hz 调用）
    /// 返回下一个要转换到的状态，或 GameState::Count 表示保持当前状态。
    virtual GameState update(float dt) = 0;

    /// 可变步长渲染（以显示器刷新率调用）
    virtual void render() = 0;
};

} // namespace melody_matrix::core

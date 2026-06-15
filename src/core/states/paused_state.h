#pragma once

#include "core/game_state_base.h"

namespace melody_matrix::core {

/// 暂停状态：游戏状态上的覆盖层。
/// 显示继续/重试/退出选项。
class PausedState : public GameStateBase {
public:
    PausedState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

private:
    void renderImGuiOverlay();

    enum class PausedAction { None, Resume, Retry, Quit };
    PausedAction m_action = PausedAction::None;
};

} // namespace melody_matrix::core

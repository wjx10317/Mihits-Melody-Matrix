// ============================================================
// paused_state.h — 暂停覆盖层状态
//
// 叠加在 Playing 状态之上，提供继续 / 重试 / 退出选项。
// 不销毁底层游玩状态，退出时根据用户选择决定后续流转。
// ============================================================
#pragma once

#include "core/game_state_base.h"

namespace melody_matrix::core {

/// 暂停状态：游戏状态上的覆盖层。
/// 显示继续/重试/退出选项。
class PausedState : public GameStateBase {
public:
    PausedState() = default;

    /// 进入暂停：重置待执行动作
    void onEnter() override;
    /// 退出暂停：记录日志
    void onExit() override;
    /// 根据用户选择返回目标状态（Playing / SongSelect）
    GameState update(float dt) override;
    /// 渲染半透明遮罩与暂停对话框
    void render() override;

private:
    /// 绘制 ImGui 暂停覆盖层（遮罩 + 继续/重试/退出按钮）
    void renderImGuiOverlay();

    /// 用户在暂停菜单中选择的动作
    enum class PausedAction { None, Resume, Retry, Quit };
    PausedAction m_action = PausedAction::None;
};

} // namespace melody_matrix::core

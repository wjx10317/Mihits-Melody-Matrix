#pragma once

#include "core/game_state_base.h"
#include <string>

namespace melody_matrix::core {

/// 结果状态：歌曲结束后显示分数详情。
class ResultState : public GameStateBase {
public:
    ResultState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

    // ── 分数数据（进入此状态前设置）──
    int score = 0;
    int maxCombo = 0;
    int perfectCount = 0;
    int goodCount = 0;
    int missCount = 0;
    int totalNotes = 0;
    bool playerDied = false;
    std::string songTitle;

private:
    void renderImGuiPanel();

    enum class ResultAction { None, Retry, Back };
    ResultAction m_action = ResultAction::None;
};

} // namespace melody_matrix::core

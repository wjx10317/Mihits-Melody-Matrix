// ============================================================
// result_state.h — 结算状态
//
// Playing 写入 score/combo/判定统计；Retry→Playing / Back→SongSelect
// ============================================================
#pragma once

#include "core/game_state_base.h"
#include <string>

namespace melody_matrix::core {

/// 结果状态：歌曲结束后显示分数详情。
class ResultState : public GameStateBase {
public:
    ResultState() = default;

    /// 进入结算：重置待执行动作
    void onEnter() override;
    /// 退出结算
    void onExit() override;
    /// 根据用户选择返回 Playing（重试）或 SongSelect（返回）
    GameState update(float dt) override;
    /// 渲染结算面板
    void render() override;

    // ── 分数数据（进入此状态前由 PlayingState 设置）──
    int score = 0;           ///< 总分
    int maxCombo = 0;        ///< 最大连击
    int hit300Count = 0;     ///< 300 数量
    int hit100Count = 0;     ///< 100 数量
    int hit50Count = 0;      ///< 50 数量
    int missCount = 0;       ///< Miss 数量
    int totalNotes = 0;      ///< 音符总数
    bool playerDied = false; ///< 是否因 HP 归零失败
    std::string songTitle;   ///< 歌曲标题

private:
    /// 绘制 ImGui 结算面板（分数 / 准确度 / 判定 / 评级 / 按钮）
    void renderImGuiPanel();

    /// 用户在结算界面中选择的动作
    enum class ResultAction { None, Retry, Back };
    ResultAction m_action = ResultAction::None;
};

} // namespace melody_matrix::core

#pragma once

#include "core/game_state_base.h"
#include "audio/audio_engine.h"
#include "gameplay/judge_queue.h"
#include "gameplay/score_manager.h"
#include "gameplay/combo_manager.h"
#include "gameplay/hp_manager.h"
#include "gameplay/formation_controller.h"
#include "beatmap/beatmap.h"

#include <SDL.h>
#include <array>
#include <memory>
#include <string>

namespace melody_matrix::core {

/// 游戏状态：玩家游玩谱面的核心游戏状态。
class PlayingState : public GameStateBase {
public:
    PlayingState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

    /// 设置要播放的谱面文件
    void setBeatmapFile(const std::string& path) { m_beatmapFile = path; }

    /// 标记在下一次 onEnter() 时应重新初始化游戏
    void markNeedsReinit() { m_needsReinit = true; }

    /// 清除渲染资源（在非 Playing 状态下退出时调用）
    void cleanupRenderer();

    /// 设置谱面背景图路径（由 SongSelectState 传入）
    void setBackgroundImage(const std::string& path) { m_bgImagePath = path; }

private:
    void initGameplay();
    void resetGameplay();
    void processInput();
    void renderHUD();
    void renderImGuiOverlay();

    struct KeyColumnMapping {
        int32_t sdlKey;
        int32_t column;
    };
    std::vector<KeyColumnMapping> getKeyMapping() const;

    void handlePressResult(gameplay::JudgmentResult result, int32_t column = 0);
    void handleHoldReleaseResult(gameplay::HoldReleaseResult result);

    // ── 游戏子系统 ──
    audio::AudioEngine m_audio;
    gameplay::JudgeQueue m_judgeQueue;
    gameplay::ScoreManager m_scoreManager;
    gameplay::ComboManager m_comboManager;
    gameplay::HPManager m_hpManager;
    gameplay::FormationController m_formationCtrl;

    // ── 谱面数据 ──
    beatmap::Beatmap m_beatmap;
    std::string m_beatmapFile;
    std::string m_bgImagePath;    ///< 谱面背景图路径
    bool m_gameplayInitialized = false;
    bool m_needsReinit = false;

    // ── 游戏状态 ──
    bool m_songFinished = false;
    bool m_playerDied = false;
    int m_totalNotes = 0;
    int m_hitNotes = 0;
    int m_perfectCount = 0;
    int m_goodCount = 0;
    int m_missCount = 0;

    // ── 输入 ──
    static constexpr int KEY_COUNT = 4;
    static constexpr int KEY_CODES[KEY_COUNT] = { SDLK_d, SDLK_f, SDLK_j, SDLK_k };
    static constexpr const char* KEY_LABELS[KEY_COUNT] = { "D", "F", "J", "K" };
    std::array<bool, KEY_COUNT> m_curKeyDown = {};  ///< 当前按键状态（供按键提示+Hold判定）

    // ── 列活跃窗口 ──
    struct ScrollWindow {
        int32_t startCol = 0;           ///< 当前活跃窗口起始列
        int32_t endCol = 3;             ///< 当前活跃窗口结束列（含）
        bool scrolling = false;         ///< 是否正在切换窗口中
        int64_t scrollStartMs = 0;      ///< 切换开始时间
        float scrollDurationMs = 200.0f;///< 切换动画时长
        int32_t targetStartCol = 0;     ///< 目标起始列
        int32_t targetEndCol = 3;       ///< 目标结束列

        /// 切换是否完成
        bool finished(int64_t nowMs) const {
            return scrolling && nowMs >= scrollStartMs + static_cast<int64_t>(scrollDurationMs);
        }
    };
    ScrollWindow m_scrollWindow;

    /// 检查是否需要滚动并触发
    void checkAndTriggerScroll(int64_t nowMs);
    /// 完成滚动，更新映射
    void completeScroll();

    // ── 前导/倒计时 ──
    static constexpr int64_t LEAD_IN_BEFORE_NOTE_MS = 3000;  ///< 第一个note前3秒开始游玩
    static constexpr int64_t SKIP_TARGET_BEFORE_MS = 3000;   ///< skip跳到第一个note前3秒
    int64_t m_firstNoteTimeMs = 0;     ///< 第一个note的时间
    bool m_leadInActive = false;       ///< 是否处于前导等待阶段
    bool m_matrixVisible = false;      ///< 矩阵是否可见

    // ── 判定弹出 ──
    struct JudgePopup {
        int32_t column = 0;
        gameplay::JudgmentResult result = gameplay::JudgmentResult::Ignored;
        float timer = 0.0f;           ///< 剩余显示时间（秒）
        static constexpr float DURATION = 0.6f;  ///< 弹出持续时间
    };
    std::vector<JudgePopup> m_popups;
};

} // namespace melody_matrix::core

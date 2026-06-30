// ============================================================
// playing_state.h — 核心游玩状态
//
// 玩家游玩谱面的主状态：负责音频播放、判定队列、分数/连击/HP、
// 列滚动、变阵过渡、Autoplay 模组与 ImGui HUD 覆盖层。
// ============================================================
#pragma once

#include "core/game_state_base.h"
#include "audio/audio_engine.h"
#include "gameplay/judge_queue.h"
#include "gameplay/score_manager.h"
#include "gameplay/combo_manager.h"
#include "gameplay/hp_manager.h"
#include "gameplay/formation_controller.h"
#include "beatmap/beatmap.h"
#include "renderer/note_renderer.h"

#include <SDL.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace melody_matrix::core {

/// 游戏状态：玩家游玩谱面的核心游戏状态。
class PlayingState : public GameStateBase {
public:
    PlayingState() = default;

    /// 进入游玩：首次初始化或从暂停/重试恢复
    void onEnter() override;
    /// 退出游玩：暂停音频与时钟（不清除渲染，供暂停覆盖层使用）
    void onExit() override;
    /// 每帧更新：时钟同步、判定、滚动、变阵、HP、结束检测
    GameState update(float dt) override;
    /// 渲染 HUD 与 ImGui 覆盖层
    void render() override;

    /// 立即处理一条键盘事件。eventTimeMs 是事件发生时换算出的歌曲时间。
    ///
    /// 按键→判定链路:
    ///   SDL KEYDOWN/UP → Kernel::syncPlayingClock → dispatchGameplayKeyEvent
    ///     → Clock::songTimeAtTickMs(SDL event timestamp)  // 与 interpolatedNowMs 同用 SDL tick
    ///     → handleKeyEvent → JudgeQueue
    /// 判定时刻 = 歌曲时间 − timingOffsetMs；音符渲染另加 visualLead 补偿显示延迟。
    void handleKeyEvent(int32_t key, bool pressed, int64_t eventTimeMs);

    /// 设置要播放的谱面文件
    void setBeatmapFile(const std::string& path) { m_beatmapFile = path; }

    /// 标记在下一次 onEnter() 时应重新初始化游戏
    void markNeedsReinit() { m_needsReinit = true; }

    /// 从音频 cursor 同步时钟（主循环处理输入前调用，保证按键 timestamp 换算准确）
    void syncClockFromAudio();

    /// 清除渲染资源（在非 Playing 状态下退出时调用）
    void cleanupRenderer();

    /// 设置谱面背景图路径（由 SongSelectState 传入）
    void setBackgroundImage(const std::string& path) { m_bgImagePath = path; }

    /// 设置启用的模组 ID 列表（由 SongSelectState 传入，在 initGameplay 中应用）
    void setMods(const std::vector<std::string>& modIds) { m_modIds = modIds; }

private:
    /// 首次加载谱面并初始化全部游戏子系统
    void initGameplay();
    /// 重置全部子系统以支持重试（不重新解析谱面路径）
    void resetGameplay();
    /// 渲染 OpenGL HUD（预留）
    void renderHUD();
    /// 渲染 ImGui 覆盖层（前导倒计时、分数、偏移条、调试 HUD）
    void renderImGuiOverlay();

    /// SDL 按键到谱面列的映射（随滚动窗口动态变化）
    struct KeyColumnMapping {
        int32_t sdlKey;
        int32_t column;
    };
    /// 根据当前滚动窗口与列数生成按键映射
    std::vector<KeyColumnMapping> getKeyMapping() const;

    /// 处理按键判定结果：更新分数/连击/HP/特效/偏移条
    void handlePressResult(gameplay::JudgmentResult result, int32_t column, int32_t row,
                           int64_t pressTime, int64_t noteTime, bool isTapNote = true);
    /// 处理 Hold 尾部释放判定
    void handleHoldTailEvent(const gameplay::HoldTailEvent& evt);

    /// 歌曲时间 → 判定时间（用户 timing offset，默认 0）
    int64_t toJudgeSongTimeMs(int64_t songTimeMs) const {
        return songTimeMs - m_timingOffsetMs;
    }

    /// Autoplay 模组：自动在 note 时间点模拟按键
    void processAutoplay(int64_t nowMs, float od);
    /// 根据列号查找对应按键索引（0~3），未映射返回 -1
    int keyIndexForColumn(int32_t column) const;
    /// 按键是否应显示为按下状态（含 Autoplay 模拟）
    bool isKeyVisuallyDown(int keyIndex) const;
    /// 变阵过渡期间是否应锁定判定（SCALE_ONLY 同形状除外）
    bool isFormationJudgmentBlocked(int64_t songTimeMs) const;

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
    std::vector<std::string> m_modIds;  ///< 启用的模组 ID 列表
    bool m_gameplayInitialized = false;
    bool m_needsReinit = false;

    // ── Autoplay 模组 ──
    bool m_autoplay = false;
    int64_t m_timingOffsetMs = 0;  ///< 正值表示音频听感偏晚，判定时间向前补偿
    bool m_debugHudEnabled = false;

    struct HitTimingDebug {
        int64_t judgeMs = 0;
        int64_t noteMs = 0;
        int64_t timing = 0;
        gameplay::JudgmentResult result = gameplay::JudgmentResult::Ignored;
    };
    HitTimingDebug m_lastHitDebug;

    static constexpr float HIT_EFFECT_DURATION = 0.28f;
    std::vector<renderer::CellHitEffect> m_hitEffects;

    // ── 偏移条（Offset Bar）──
    bool m_offsetBarEnabled = false;
    struct OffsetBarMark {
        int64_t hitTime = 0;                       ///< 击打时刻
        int64_t noteTime = 0;                      ///< note 时间
        int64_t timing = 0;                        ///< 判定时机偏差(hitTime - noteTime)
        float timer = 0.0f;                        ///< 显示倒计时
        gameplay::JudgmentResult result = gameplay::JudgmentResult::Ignored;  ///< 判定结果
        static constexpr float DURATION = 2.0f;    ///< 显示 2 秒
    };
    std::vector<OffsetBarMark> m_offsetBarMarks;

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
    std::array<bool, KEY_COUNT> m_curKeyDown = {};   ///< 玩家物理按键（供视觉反馈）
    std::array<bool, KEY_COUNT> m_autoKeyDown = {}; ///< Autoplay 模拟按键（供视觉反馈）
    std::array<float, KEY_COUNT> m_autoKeyFlash = {}; ///< Tap 击打后短暂高亮（秒）
    static constexpr float AUTO_KEY_FLASH_SEC = 0.09f;

    // ── 列活跃窗口 ──
    struct ScrollWindow {
        int32_t startCol = 0;           ///< 当前活跃窗口起始列
        int32_t endCol = 3;             ///< 当前活跃窗口结束列（含）
        bool scrolling = false;         ///< 是否正在切换窗口中
        int64_t scrollStartMs = 0;      ///< 切换开始时间
        float scrollDurationMs = 200.0f;///< 切换动画时长（自适应）
        int32_t targetStartCol = 0;     ///< 目标起始列
        int32_t targetEndCol = 3;       ///< 目标结束列
        float scrollOffset = 0.0f;      ///< 当前滚动偏移（像素，用于动画）

        /// 切换是否完成（nowMs 超过 scrollStart + duration）
        bool finished(int64_t nowMs) const {
            return scrolling && nowMs >= scrollStartMs + static_cast<int64_t>(scrollDurationMs);
        }

        /// 滚动进度 [0,1]，供 renderer 插值列偏移
        float progress(int64_t nowMs) const {
            if (!scrolling || scrollDurationMs <= 0.0f) return 1.0f;  // 非滚动视为已完成
            float p = static_cast<float>(nowMs - scrollStartMs) / scrollDurationMs;
            return std::max(0.0f, std::min(1.0f, p));                // clamp 到 [0,1]
        }
    };
    ScrollWindow m_scrollWindow;
    int64_t m_lastTransitionEndMs = 0;  ///< 上次滚动/变阵结束时刻（对齐 parser lastTransitionEnd）

    /// 检查是否需要滚动并触发
    void checkAndTriggerScroll(int64_t nowMs);
    /// 完成滚动，更新映射
    void completeScroll();
    /// 变阵后将滚动窗口对齐到包含触发 note 的有效窗（与 parser chooseScrollActiveStart 一致）
    void snapScrollWindowForFormation(int32_t newCols, int64_t formationTimeMs);

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

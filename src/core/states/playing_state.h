#pragma once

#include "core/game_state_base.h"
#include "audio/audio_engine.h"
#include "gameplay/judge_queue.h"
#include "gameplay/score_manager.h"
#include "gameplay/combo_manager.h"
#include "gameplay/hp_manager.h"
#include "gameplay/formation_controller.h"
#include "input/input_manager.h"
#include "beatmap/beatmap.h"

#include <memory>
#include <string>

namespace melody_matrix::core {

/// 游戏状态：玩家游玩谱面的核心游戏状态。
/// 连接音频、判定、分数、连击、生命值、阵型和输入系统。
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
    /// 用于从结果/暂停状态重试歌曲时。
    void markNeedsReinit() { m_needsReinit = true; }

private:
    void initGameplay();
    void resetGameplay();
    void processInput();
    void renderHUD();
    void renderImGuiOverlay();

    // ── 游戏子系统 ──
    audio::AudioEngine m_audio;
    gameplay::JudgeQueue m_judgeQueue;
    gameplay::ScoreManager m_scoreManager;
    gameplay::ComboManager m_comboManager;
    gameplay::HPManager m_hpManager;
    gameplay::FormationController m_formationCtrl;
    input::InputManager m_inputManager;

    // ── 谱面数据 ──
    beatmap::Beatmap m_beatmap;
    std::string m_beatmapFile;
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
};

} // namespace melody_matrix::core

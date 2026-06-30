// ============================================================
// playing_state.cpp — 核心游玩状态实现
//
// 职责：
//   - 谱面加载、音频播放与时钟同步
//   - 按键输入 → 判定队列 → 分数/连击/HP 反馈
//   - 列滚动窗口与变阵过渡动画
//   - Autoplay / NoFail 模组
//   - 前导倒计时、休息段淡出、结算过渡
// ============================================================
#include "playing_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/result_state.h"
#include "beatmap/scroll_simulation.h"
#include "platform/config.h"
#include "ui/theme.h"
#include "util/logger.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/beatmap_builder.h"
#include "renderer/grid_layout.h"
#include <algorithm>
#include <SDL.h>
#include "platform/file_system.h"

#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace melody_matrix::core {

constexpr int PlayingState::KEY_CODES[];

// ══════════════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════════════

/// 进入游玩状态：首次 initGameplay 或从暂停恢复
void PlayingState::onEnter() {
    MM_LOG_INFO("Playing", "Entering Playing state");

    // 禁用 IME，防止中文输入法拦截 KEYDOWN 事件
    SDL_StopTextInput();

    if (m_needsReinit && m_gameplayInitialized) {
        resetGameplay();
    }

    if (m_gameplayInitialized) {
        // 歌曲已结束则不再恢复音频（防止暂停后继续导致音乐重播）
        if (!m_songFinished) {
            m_audio.resume();
            Kernel::instance().clock().resume();
        }
        Kernel::instance().renderer().setGameplayRendering(true);

        // 恢复时重置按键状态，避免暂停期间按下的键在恢复后误触发
        m_curKeyDown = {};
        m_autoKeyDown = {};
        m_autoKeyFlash = {};

        MM_LOG_INFO("Playing", "Resuming playback");
        return;
    }

    initGameplay();
}

/// 退出游玩状态：暂停音频与时钟，恢复 IME
void PlayingState::onExit() {
    MM_LOG_INFO("Playing", "Exiting Playing state");
    if (m_audio.isPlaying()) {
        m_audio.pause();
    }
    Kernel::instance().clock().pause();
    // 恢复 IME，供其他界面（如搜索框）使用
    SDL_StartTextInput();
    // 注意：不清除背景和gameplay渲染，暂停时仍需显示
    // StateManager::executeTransition() 会根据目标状态设置 gameplayRendering
}

/// 清除 OpenGL 渲染资源（从 Paused/Result 退出到选歌时调用）
void PlayingState::cleanupRenderer() {
    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);
    renderer.setBackgroundPath("");
}

// ══════════════════════════════════════════════════════════════════════════════
//  初始化 / 重置
// ══════════════════════════════════════════════════════════════════════════════

/// 重置全部子系统以支持重试
void PlayingState::resetGameplay() {
    MM_LOG_INFO("Playing", "Resetting gameplay for retry");

    m_audio.stop();
    m_audio.shutdown();

    // 重置时钟，确保不在暂停状态
    Kernel::instance().clock().reset();

    m_judgeQueue.reset();
    m_scoreManager.reset();
    m_comboManager.reset();
    m_hpManager.reset();
    m_formationCtrl.reset();

    m_gameplayInitialized = false;
    m_songFinished = false;
    m_playerDied = false;
    m_totalNotes = 0;
    m_hitNotes = 0;
    m_perfectCount = 0;
    m_goodCount = 0;
    m_missCount = 0;
    m_needsReinit = false;
    m_leadInActive = false;
    m_matrixVisible = false;
    m_curKeyDown = {};
    m_autoKeyDown = {};
    m_autoKeyFlash = {};
    m_popups.clear();

    m_scrollWindow = {};  // 重置滚动窗口
    m_lastTransitionEndMs = 0;

    m_autoplay = false;
    m_timingOffsetMs = 0;
    m_offsetBarEnabled = false;
    m_offsetBarMarks.clear();
    m_hitEffects.clear();
    m_lastHitDebug = {};

    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);
    renderer.setBackgroundPath("");

    MM_LOG_INFO("Playing", "Gameplay reset complete");
}

/// 首次加载谱面：解析、初始化判定/分数/HP/变阵、启动音频
void PlayingState::initGameplay() {
    if (m_gameplayInitialized) return;

    MM_LOG_INFO("Playing", "Initializing gameplay with: " + m_beatmapFile);

    // ── Init audio ──
    if (!m_audio.init()) {
        MM_LOG_ERROR("Playing", "Failed to initialize audio engine");
        return;
    }
    m_audio.loadSfx();

    // ── Reset clock ──
    // 从 Result/Retry 回来时时钟可能处于暂停状态，必须重置
    Kernel::instance().clock().reset();

    // ── Load beatmap ──
    auto parser = beatmap::createParserForFile(m_beatmapFile);
    if (!parser) {
        MM_LOG_ERROR("Playing", "No parser available for: " + m_beatmapFile);
        return;
    }

    std::ifstream ifs(m_beatmapFile);
    if (!ifs.is_open()) {
        MM_LOG_ERROR("Playing", "Cannot open file: " + m_beatmapFile);
        return;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    beatmap::BeatmapBuilder builder;
    auto parseResult = parser->parse(content, builder);
    if (!parseResult.ok()) {
        MM_LOG_ERROR("Playing", "Failed to parse beatmap: " + parseResult.error().message);
        return;
    }

    auto buildResult = builder.build();
    if (!buildResult.ok()) {
        MM_LOG_ERROR("Playing", "Beatmap validation failed: " + buildResult.error().message);
        return;
    }
    m_beatmap = std::move(buildResult.value());

    // ── Init judge ──
    m_judgeQueue.setStrategy(std::make_unique<gameplay::StandardJudgeStrategy>());
    m_judgeQueue.loadNotes(m_beatmap.notes);

    // ── 连接 JudgeQueue 事件回调 ──
    m_judgeQueue.onHit = [this](const gameplay::NoteHitEvent& evt) {
        MM_LOG_INFO("Playing", "Hit: col=" + std::to_string(evt.col) +
                    " result=" + std::to_string(static_cast<int>(evt.result)) +
                    " dt=" + std::to_string(evt.pressTime - evt.time) + "ms");
    };
    m_judgeQueue.onMiss = [this](const gameplay::NoteMissEvent& evt) {
        MM_LOG_INFO("Playing", "Miss: col=" + std::to_string(evt.col));
        m_missCount++;
        m_comboManager.onMiss();
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        m_popups.push_back({evt.col, gameplay::JudgmentResult::Miss, JudgePopup::DURATION});

        // 偏移条：记录 auto-miss（timing 取 goodW 边界值，超出 good 窗口）
        if (m_offsetBarEnabled) {
            float od = m_beatmap.difficulty.od;
            int32_t goodW = static_cast<int32_t>(65.0f - 2.6f * od);
            int64_t missTiming = static_cast<int64_t>(goodW) + 50;
            m_offsetBarMarks.push_back({evt.time + missTiming, evt.time,
                                        missTiming,
                                        OffsetBarMark::DURATION,
                                        gameplay::JudgmentResult::Miss});
        }
    };
    m_judgeQueue.onHoldTail = [this](const gameplay::HoldTailEvent& evt) {
        handleHoldTailEvent(evt);
    };

    // ── Init formation ──
    m_formationCtrl.load(m_beatmap.formations);

    // ── 初始化滚动窗口 ──
    int32_t initCols = m_formationCtrl.currentCols();
    if (initCols > KEY_COUNT) {
        m_scrollWindow.startCol = 0;
        m_scrollWindow.endCol = KEY_COUNT - 1;
    } else {
        m_scrollWindow.startCol = 0;
        m_scrollWindow.endCol = initCols - 1;
    }
    m_scrollWindow.scrolling = false;
    m_lastTransitionEndMs = 0;

    // ── 设置 HP drain rate ──
    m_hpManager.setDrainRate(m_beatmap.difficulty.hp);

    // ── 应用模组 ──
    bool noFailEnabled = false;
    m_autoplay = false;
    for (const auto& id : m_modIds) {
        if (id == "nofail") {
            noFailEnabled = true;
        } else if (id == "autoplay") {
            m_autoplay = true;
        }
    }
    if (noFailEnabled) {
        m_hpManager.setMod(std::make_shared<gameplay::NoFailMod>());
        MM_LOG_INFO("Playing", "NoFail mod enabled");
    } else {
        m_hpManager.setMod(nullptr);
    }
    if (m_autoplay) {
        MM_LOG_INFO("Playing", "Autoplay mod enabled");
    }

    // ── 读取偏移条配置 ──
    // 正值表示音频听感偏晚：判定时间整体向前修正，视觉/滚动仍使用原始歌曲时间。
    m_timingOffsetMs = platform::Config::getInt(platform::Config::KEY_TIMING_OFFSET, 0);
    m_debugHudEnabled = platform::Config::getInt(platform::Config::KEY_DEBUG_HUD, 0) != 0;
    m_offsetBarEnabled = platform::Config::getInt(platform::Config::KEY_OFFSET_BAR, 0) != 0;
    m_offsetBarMarks.clear();

    // ── Count notes ──
    m_totalNotes = static_cast<int>(m_beatmap.notes.size());

    // ── 计算第一个note的时间 ──
    m_firstNoteTimeMs = 0;
    if (!m_beatmap.notes.empty()) {
        m_firstNoteTimeMs = m_beatmap.notes.front().time;
    }

    // ── 前导倒计时：基于第一个note的时间 ──
    // 如果第一个note在歌曲开始后很晚才出现，需要等待
    m_leadInActive = true;
    m_matrixVisible = false;

    // ── 设置 Renderer ──
    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);

    // 应用背景遮罩透明度（从配置读取）
    float bgDim = platform::Config::getFloat(platform::Config::KEY_BG_DIM, 0.67f);
    renderer.setBgDim(bgDim);

    // 设置背景（优先.mp4其次图片，当前仅图片）
    if (!m_bgImagePath.empty()) {
        renderer.setBackgroundPath(m_bgImagePath);
    }

    // 设置初始阵型
    if (!m_beatmap.formations.empty()) {
        const auto& f = m_beatmap.formations[0];
        renderer.setFormation(f.rows, f.cols, f.blockSize);
    }

    // 设置音符数据
    renderer.setNotes(m_beatmap.notes, m_beatmap.difficulty.ar);

    // ── Start playback ──
    // 音频文件路径：优先使用绝对路径，否则相对于谱面文件目录解析
    std::string audioPath = m_beatmap.meta.audioFile;
    if (!audioPath.empty()) {
        // 尝试相对于谱面文件目录解析
        auto absPath = std::filesystem::path(m_beatmapFile).parent_path() / audioPath;
        if (std::filesystem::exists(absPath)) {
            audioPath = std::filesystem::absolute(absPath).string();
        }
    }
    if (!m_audio.playSong(audioPath)) {
        MM_LOG_WARN("Playing", "Failed to play: " + audioPath);
        auto resolved = platform::FileSystem::safeResolve("assets", m_beatmap.meta.audioFile);
        if (resolved.ok()) {
            if (!m_audio.playSong(resolved.value())) {
                MM_LOG_WARN("Playing", "Audio playback failed - continuing without audio");
            }
        }
    }

    m_gameplayInitialized = true;
    m_needsReinit = false;
    MM_LOG_INFO("Playing", "Gameplay initialized - " + std::to_string(m_totalNotes) +
                " notes, first note at " + std::to_string(m_firstNoteTimeMs) + "ms");
}

/// 从音频 cursor 同步游戏时钟（主循环处理输入前调用）
void PlayingState::syncClockFromAudio() {
    if (!m_gameplayInitialized) return;
    Kernel::instance().clock().syncFromAudio(m_audio.positionMs());
}

// ══════════════════════════════════════════════════════════════════════════════
//  更新
// ══════════════════════════════════════════════════════════════════════════════

/// 每帧更新：时钟、前导、判定、滚动、变阵、HP、结束检测
GameState PlayingState::update(float dt) {
    if (!m_gameplayInitialized) return GameState::Count;

    // ── Sync clock from audio ──
    auto& kernel = Kernel::instance();
    syncClockFromAudio();

    int64_t nowMs = kernel.clock().interpolatedNowMs();
    const int64_t judgeNowMs = toJudgeSongTimeMs(nowMs);
    float od = m_beatmap.difficulty.od;

    // ── Skip 功能：空格键跳过前导 ──
    if (m_leadInActive) {
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);
        SDL_Scancode spaceScan = SDL_GetScancodeFromKey(SDLK_SPACE);
        if (keyState[spaceScan]) {
            // 跳到第一个note前 SKIP_TARGET_BEFORE_MS 毫秒
            int64_t skipTarget = m_firstNoteTimeMs - SKIP_TARGET_BEFORE_MS;
            if (skipTarget > 0 && skipTarget > nowMs) {
                // seek 音频到目标位置
                m_audio.seekTo(skipTarget);
                kernel.clock().syncFromAudio(m_audio.positionMs());
                nowMs = skipTarget;
                MM_LOG_INFO("Playing", "Skipped to " + std::to_string(skipTarget) + "ms");
            }
        }
    }

    // ── 前导等待：直到音频时间接近第一个note ──
    if (m_leadInActive) {
        int64_t gameplayStartMs = m_firstNoteTimeMs - LEAD_IN_BEFORE_NOTE_MS;
        if (nowMs >= gameplayStartMs) {
            m_leadInActive = false;
            MM_LOG_INFO("Playing", "Lead-in complete, nowMs=" + std::to_string(nowMs) +
                        " audioPos=" + std::to_string(m_audio.positionMs()) +
                        " firstNote=" + std::to_string(m_firstNoteTimeMs) + "ms");
        }
        // 前导期间不处理输入和判定
        kernel.renderer().setGameplayRendering(false);
        return GameState::Count;
    }

    // ── 矩阵可见性：第一个note即将出现时才显示 ──
    if (!m_matrixVisible) {
        // 当音频时间接近第一个note时显示矩阵
        float approachMs = 1800.0f - m_beatmap.difficulty.ar * 120.0f;
        if (approachMs < 300.0f) approachMs = 300.0f;
        if (nowMs >= m_firstNoteTimeMs - static_cast<int64_t>(approachMs)) {
            m_matrixVisible = true;
        }
    }

    // ── Process input & update judge queue ──
    // 滚动与改变行列结构的变换期间锁判定；SCALE_ONLY 只缩放 item，允许继续判定。
    bool inTransition = m_formationCtrl.inTransition(nowMs);
    bool inScroll = m_scrollWindow.scrolling;
    bool judgmentBlocked = inScroll || isFormationJudgmentBlocked(nowMs);
    if (!judgmentBlocked) {
        if (m_autoplay) {
            for (int k = 0; k < KEY_COUNT; ++k) {
                if (m_autoKeyFlash[k] > 0.0f) {
                    m_autoKeyFlash[k] -= dt;
                }
            }
            processAutoplay(nowMs, od);
        }
        m_judgeQueue.update(judgeNowMs, od);
    }

    // ── 列滚动逻辑 ──
    // 检查滚动是否完成
    if (inScroll && m_scrollWindow.finished(nowMs)) {
        completeScroll();
    }
    // 检查是否需要触发滚动（滚动与变换不能重叠）
    if (!inScroll && !inTransition) {
        checkAndTriggerScroll(nowMs);
    }
    // 同步滚动状态到渲染器。scrollOffset 由 renderer 内部根据 scrollProgress 和 m_gridCols 统一计算，
    // 确保 renderGrid / renderNotes / note_renderer 三处 gw 基准完全一致（消除抽搐和 note 错位）。
    kernel.renderer().setScrollState(m_scrollWindow.startCol, m_scrollWindow.endCol,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetStartCol : m_scrollWindow.startCol,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetEndCol : m_scrollWindow.endCol,
                                      m_scrollWindow.scrolling, m_scrollWindow.progress(nowMs));
    {
        std::array<size_t, 8> heads = {};
        for (int32_t c = 0; c < m_judgeQueue.columnCount() && c < 8; ++c) {
            heads[c] = m_judgeQueue.columnQueue(c).head;
        }
        kernel.renderer().setColumnHeads(heads, m_judgeQueue.columnCount());
        kernel.renderer().setHitEffects(m_hitEffects);
    }

    // ── 休息段检测：>10s 空挡渐变隐藏游戏界面，新 note 前 3s 渐变回来 ──
    // beatmap.notes 已按时间排序，找当前时间后的第一个 note 和前的最后一个 note
    if (!m_beatmap.notes.empty()) {
        int64_t nextNoteTime = INT64_MAX;
        int64_t prevNoteTime = 0;
        for (const auto& n : m_beatmap.notes) {
            if (n.time > nowMs) { nextNoteTime = n.time; break; }
            prevNoteTime = n.time;
        }

        float gameplayFade = 1.0f;
        if (nextNoteTime != INT64_MAX && prevNoteTime > 0) {
            int64_t gap = nextNoteTime - prevNoteTime;
            if (gap > 10000) {
                // prevNote+1s 开始 1s 内渐变隐藏；nextNote-3s 开始 1s 内渐变回来
                int64_t fadeOutStart = prevNoteTime + 1000;
                int64_t fadeInStart = nextNoteTime - 3000;
                int64_t fadeInEnd = nextNoteTime - 2000;
                if (nowMs < fadeOutStart) {
                    gameplayFade = 1.0f;
                } else if (nowMs < fadeOutStart + 1000) {
                    gameplayFade = 1.0f - static_cast<float>(nowMs - fadeOutStart) / 1000.0f;
                } else if (nowMs < fadeInStart) {
                    gameplayFade = 0.0f;
                } else if (nowMs < fadeInEnd) {
                    gameplayFade = static_cast<float>(nowMs - fadeInStart) / 1000.0f;
                } else {
                    gameplayFade = 1.0f;
                }
            }
        }
        kernel.renderer().setGameplayFade(gameplayFade);
    }

    // ── Update formation ──
    bool formationChanged = m_formationCtrl.update(nowMs);
    if (formationChanged) {
        const auto& prev = m_formationCtrl.formationAt(
            m_formationCtrl.currentIndex() > 0 ? m_formationCtrl.currentIndex() - 1 : 0);
        const auto& next = m_formationCtrl.current();
        int64_t durationMs = next.transformDurationMs;
        if (durationMs > 0) {
            // 启动过渡动画
            m_formationCtrl.setTransitionDuration(durationMs);
            kernel.renderer().beginFormationTransition(prev.rows, prev.cols, prev.blockSize,
                                                       next.rows, next.cols, next.blockSize,
                                                       next.transformType);
            m_lastTransitionEndMs = std::max(m_lastTransitionEndMs, next.time + durationMs);
        } else {
            // 瞬间切换
            kernel.renderer().setFormation(next.rows, next.cols, next.blockSize);
            m_lastTransitionEndMs = std::max(m_lastTransitionEndMs, next.time);
        }

        // 变阵后对齐到有效窗口（非强制居中）
        snapScrollWindowForFormation(next.cols, next.time);
        m_scrollWindow.targetStartCol = m_scrollWindow.startCol;
        m_scrollWindow.targetEndCol = m_scrollWindow.endCol;
        m_scrollWindow.scrolling = false;
    }

    // ── Update formation transition progress ──
    if (inTransition) {
        float progress = m_formationCtrl.transitionProgress(nowMs);
        kernel.renderer().updateFormationTransition(progress);
    }

    // ── HP passive drain ──
    m_hpManager.drainPerFrame(dt);

    // ── Update popups ──
    for (auto& p : m_popups) {
        p.timer -= dt;
    }
    m_popups.erase(
        std::remove_if(m_popups.begin(), m_popups.end(),
                       [](const JudgePopup& p) { return p.timer <= 0.0f; }),
        m_popups.end());

    for (auto& hit : m_hitEffects) {
        hit.alpha -= dt / HIT_EFFECT_DURATION;
    }
    m_hitEffects.erase(
        std::remove_if(m_hitEffects.begin(), m_hitEffects.end(),
                       [](const renderer::CellHitEffect& h) { return h.alpha <= 0.0f; }),
        m_hitEffects.end());

    // ── Update offset bar marks ──
    if (m_offsetBarEnabled) {
        for (auto& m : m_offsetBarMarks) {
            m.timer -= dt;
        }
        m_offsetBarMarks.erase(
            std::remove_if(m_offsetBarMarks.begin(), m_offsetBarMarks.end(),
                           [](const OffsetBarMark& m) { return m.timer <= 0.0f; }),
            m_offsetBarMarks.end());
    }

    // ── Check song end ──
    // BGM 播放结束（ma_sound_is_playing 返回 false），
    // 或所有 note 已判定完毕且音频已停止（音频可能短于最后一个 note）
    if (!m_audio.isPlaying()) {
        if (m_audio.positionMs() > 0 || m_judgeQueue.finished()) {
            m_songFinished = true;
        }
    }

    // ── Check HP death ──
    if (m_hpManager.isDead()) {
        m_playerDied = true;
    }

    // ── State transitions ──
    if (m_songFinished || m_playerDied) {
        auto* result = kernel.stateManager().getStateAs<ResultState>(GameState::Result);
        if (result) {
            result->score = static_cast<int>(m_scoreManager.totalScore());
            result->maxCombo = m_comboManager.max();
            result->perfectCount = m_perfectCount;
            result->goodCount = m_goodCount;
            result->missCount = m_missCount;
            result->totalNotes = m_totalNotes;
            result->playerDied = m_playerDied;
            result->songTitle = m_beatmap.meta.title;
        }
        return GameState::Result;
    }

    // 在 renderFrame 之前同步，避免本帧仍用上一帧的 gameplayRendering
    kernel.renderer().setGameplayRendering(m_matrixVisible);

    return GameState::Count;
}

// ══════════════════════════════════════════════════════════════════════════════
//  输入 / 按键映射
// ══════════════════════════════════════════════════════════════════════════════

/// 根据当前滚动窗口与总列数生成 D/F/J/K → 列映射
std::vector<PlayingState::KeyColumnMapping> PlayingState::getKeyMapping() const {
    int32_t totalCols = m_formationCtrl.currentCols();
    if (totalCols <= 0) totalCols = 4;

    std::vector<KeyColumnMapping> mapping;

    // 动态映射：用 KEY_COUNT 循环，不硬编码偏移
    int32_t startCol = (totalCols <= KEY_COUNT) ? 0 : m_scrollWindow.startCol;
    int32_t mapCount = std::min(static_cast<int32_t>(KEY_COUNT), totalCols);
    for (int32_t i = 0; i < mapCount; ++i) {
        mapping.push_back({KEY_CODES[i], startCol + i});
    }

    return mapping;
}

/// 根据列号查找对应按键索引
int PlayingState::keyIndexForColumn(int32_t column) const {
    auto mapping = getKeyMapping();
    for (const auto& m : mapping) {
        if (m.column != column) continue;
        for (int k = 0; k < KEY_COUNT; ++k) {
            if (m.sdlKey == KEY_CODES[k]) return k;
        }
    }
    return -1;
}

/// 按键是否应显示为按下（Autoplay 含模拟与短暂闪烁）
bool PlayingState::isKeyVisuallyDown(int keyIndex) const {
    if (keyIndex < 0 || keyIndex >= KEY_COUNT) return false;
    if (m_autoplay) {
        return m_autoKeyDown[keyIndex] || m_autoKeyFlash[keyIndex] > 0.0f;
    }
    return m_curKeyDown[keyIndex];
}

/// 变阵过渡期间是否锁定判定（SCALE_ONLY 且行列不变时仍允许判定）
bool PlayingState::isFormationJudgmentBlocked(int64_t songTimeMs) const {
    if (!m_formationCtrl.inTransition(songTimeMs)) return false;

    const size_t currentIndex = m_formationCtrl.currentIndex();
    if (currentIndex == 0) return false;

    const auto& prev = m_formationCtrl.formationAt(currentIndex - 1);
    const auto& current = m_formationCtrl.current();
    const bool scaleOnly = current.transformType == beatmap::MatrixTransform::SCALE_ONLY;
    const bool sameShape = prev.rows == current.rows && prev.cols == current.cols;
    return !(scaleOnly && sameShape);
}

/// Autoplay 模组：在 note 时间点自动模拟按键与 Hold 释放
void PlayingState::processAutoplay(int64_t nowMs, float od) {
    m_autoKeyDown.fill(false);

    int32_t startCol = m_scrollWindow.startCol;
    int32_t endCol = m_scrollWindow.endCol;
    for (int32_t c = startCol; c <= endCol; ++c) {
        if (c < 0 || c >= m_judgeQueue.columnCount()) continue;

        const int keyIdx = keyIndexForColumn(c);

        const auto* activeHold = m_judgeQueue.getActiveHold(c);
        if (activeHold && nowMs >= activeHold->holdEnd) {
            m_judgeQueue.onKeyRelease(activeHold->holdEnd, c, od);
            continue;
        }
        if (activeHold) {
            if (keyIdx >= 0) m_autoKeyDown[keyIdx] = true;
            continue;
        }

        const auto& colQ = m_judgeQueue.columnQueue(c);
        if (colQ.finished()) continue;
        const auto& note = colQ.front();

        if (nowMs >= note.time) {
            int64_t noteTime = note.time;
            auto result = m_judgeQueue.onKeyPress(noteTime, c, od);
            if (result != gameplay::JudgmentResult::Ignored) {
                handlePressResult(result, c, note.row, noteTime, noteTime, !note.isHold());
                if (keyIdx >= 0) {
                    if (note.isHold()) {
                        m_autoKeyDown[keyIdx] = true;
                    } else {
                        m_autoKeyFlash[keyIdx] = AUTO_KEY_FLASH_SEC;
                    }
                }
            }
        }
    }

    for (int k = 0; k < KEY_COUNT; ++k) {
        if (m_autoKeyFlash[k] > 0.0f) {
            m_autoKeyDown[k] = true;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  列滚动
// ══════════════════════════════════════════════════════════════════════════════

/// 检测 approach 窗口内最早 note 是否超出当前窗口，触发列滚动
void PlayingState::checkAndTriggerScroll(int64_t nowMs) {
    int32_t totalCols = m_formationCtrl.currentCols();
    if (totalCols <= KEY_COUNT) return;  // 不需要滚动

    // 有活跃 Hold 时硬阻止滚动，和 parser 的 Hold 阻塞/降级策略保持一致。
    for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
        if (m_judgeQueue.getActiveHold(col)) {
            return;
        }
    }

    const int64_t approachMs = beatmap::approachMs(m_beatmap.difficulty.ar);

    int32_t windowStart = m_scrollWindow.startCol;
    int32_t windowEnd = m_scrollWindow.endCol;

    // ── 只跟踪 approach 窗口内最早需要判定的 note 所在列 ──
    // 修复：原逻辑用 neededStart/neededEnd 跨度触发滚动，会导致向左滚动时让右侧 note 离开窗口丢失。
    // 新逻辑：只滚动到包含最早判定 note 的窗口，确保最早 note 必在窗口内。
    // 时间向前流动，最早 note 需优先被击打；其他 note 若不在窗口内，等当前 note 判定完后再次触发滚动。
    int64_t earliestNoteTime = INT64_MAX;
    int32_t earliestNoteCol = -1;

    for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
        const auto& colQ = m_judgeQueue.columnQueue(col);
        if (colQ.finished()) continue;

        const auto& note = colQ.front();
        const float timeDiff = static_cast<float>(note.time - nowMs);

        // 仅对未过期 note（timeDiff > 0）且在 approach 窗口内触发滚动
        if (timeDiff <= static_cast<float>(approachMs) && timeDiff > 0) {
            if (note.time < earliestNoteTime) {
                earliestNoteTime = note.time;
                earliestNoteCol = col;
            }
        }
    }

    // 如果最早 note 不在当前窗口内，触发滚动到包含它的窗口
    if (earliestNoteCol >= 0 &&
        (earliestNoteCol < windowStart || earliestNoteCol > windowEnd)) {
        const float od = m_beatmap.difficulty.od;
        const float ar = m_beatmap.difficulty.ar;
        const int64_t scrollTrigger =
            beatmap::scrollTriggerMs(earliestNoteTime, m_lastTransitionEndMs, ar);
        const int64_t blockedStart = beatmap::scrollStartMsFromWindowHolds(
            windowStart, windowEnd, earliestNoteTime, scrollTrigger, od, m_beatmap.notes);
        if (nowMs < blockedStart) {
            return;
        }
        const int64_t scrollStartMs = std::max(nowMs, blockedStart);

        const int32_t targetStart = beatmap::chooseScrollWindowStart(
            windowStart, earliestNoteCol, totalCols, KEY_COUNT,
            earliestNoteTime, m_beatmap.notes);

        // 只在窗口确实需要移动时才触发
        if (targetStart != windowStart) {
            const float scrollDuration = static_cast<float>(beatmap::scrollDurationMs(
                earliestNoteTime, scrollStartMs, od));

            m_scrollWindow.scrolling = true;
            m_scrollWindow.scrollStartMs = scrollStartMs;
            m_scrollWindow.scrollDurationMs = scrollDuration;
            m_scrollWindow.targetStartCol = targetStart;
            m_scrollWindow.targetEndCol = targetStart + KEY_COUNT - 1;

            MM_LOG_INFO("Playing", "Scroll triggered: " +
                        std::to_string(windowStart) + "-" + std::to_string(windowEnd) +
                        " -> " + std::to_string(targetStart) + "-" +
                        std::to_string(targetStart + KEY_COUNT - 1) +
                        " (earliest note col=" + std::to_string(earliestNoteCol) +
                        " t=" + std::to_string(earliestNoteTime) + ")" +
                        " at t=" + std::to_string(scrollStartMs) +
                        "ms duration=" + std::to_string(static_cast<int>(scrollDuration)) + "ms");
        }
    }
}

/// 变阵后将滚动窗口对齐到包含触发 note 的有效窗
void PlayingState::snapScrollWindowForFormation(int32_t newCols, int64_t formationTimeMs) {
    if (newCols <= KEY_COUNT) {
        m_scrollWindow.startCol = 0;
        m_scrollWindow.endCol = newCols - 1;
        return;
    }

    int64_t anchorTime = formationTimeMs;
    int32_t anchorCol = 0;
    bool found = false;
    for (const auto& note : m_beatmap.notes) {
        if (note.time >= formationTimeMs) {
            anchorTime = note.time;
            anchorCol = note.col;
            found = true;
            break;
        }
    }
    if (!found && !m_beatmap.notes.empty()) {
        const auto& last = m_beatmap.notes.back();
        anchorTime = last.time;
        anchorCol = last.col;
    }

    const int32_t targetStart = beatmap::chooseScrollWindowStart(
        m_scrollWindow.startCol, anchorCol, newCols, KEY_COUNT, anchorTime, m_beatmap.notes);
    m_scrollWindow.startCol = targetStart;
    m_scrollWindow.endCol = targetStart + KEY_COUNT - 1;
}

/// 滚动动画完成：更新窗口起始/结束列
void PlayingState::completeScroll() {
    int32_t newStart = m_scrollWindow.targetStartCol;
    int32_t newEnd = m_scrollWindow.targetEndCol;

    // 映射改变方案：不 move note，只更新窗口；窗外冲突 note 应在 parser 转换期丢弃。
    m_scrollWindow.startCol = newStart;
    m_scrollWindow.endCol = newEnd;
    m_scrollWindow.scrolling = false;
    m_lastTransitionEndMs = std::max(
        m_lastTransitionEndMs,
        m_scrollWindow.scrollStartMs + static_cast<int64_t>(m_scrollWindow.scrollDurationMs));

    MM_LOG_INFO("Playing", "Scroll completed: window now " +
                std::to_string(m_scrollWindow.startCol) + "-" +
                std::to_string(m_scrollWindow.endCol));
}

/// 处理单条键盘事件（由 Kernel 在同步时钟后调用）
void PlayingState::handleKeyEvent(int32_t key, bool pressed, int64_t eventTimeMs) {
    if (!m_gameplayInitialized || m_leadInActive) return;
    if (m_autoplay) return;  // Autoplay 期间锁定玩家输入

    const int64_t judgeEventTimeMs = toJudgeSongTimeMs(eventTimeMs);

    // 更新按键显示状态即使暂时不判定，避免 HUD 与物理按键脱节。
    int keyIndex = -1;
    for (int k = 0; k < KEY_COUNT; ++k) {
        if (key == KEY_CODES[k]) {
            keyIndex = k;
            m_curKeyDown[k] = pressed;
            break;
        }
    }
    if (keyIndex < 0) return;

    auto mapping = getKeyMapping();
    int32_t column = -1;
    if (isFormationJudgmentBlocked(eventTimeMs) || m_scrollWindow.scrolling) {
        return;
    }
    for (const auto& m : mapping) {
        if (m.sdlKey == key) {
            column = m.column;
            break;
        }
    }
    if (column < 0) return;

    const float od = m_beatmap.difficulty.od;
    if (pressed) {
        int64_t noteTime = 0;
        int32_t noteRow = 0;
        bool isTapNote = true;
        if (column >= 0 && column < m_judgeQueue.columnCount()) {
            const auto& colQ = m_judgeQueue.columnQueue(column);
            if (!colQ.finished()) {
                noteTime = colQ.front().time;
                noteRow = colQ.front().row;
                isTapNote = !colQ.front().isHold();
            }
        }
        auto result = m_judgeQueue.onKeyPress(judgeEventTimeMs, column, od);
        MM_LOG_INFO("Playing", "KeyDown: col=" + std::to_string(column) +
                    " eventTimeMs=" + std::to_string(eventTimeMs) +
                    " judgeTimeMs=" + std::to_string(judgeEventTimeMs) +
                    " result=" + std::to_string(static_cast<int>(result)));
        handlePressResult(result, column, noteRow, judgeEventTimeMs, noteTime, isTapNote);
    } else {
        m_judgeQueue.onKeyRelease(judgeEventTimeMs, column, od);
        MM_LOG_INFO("Playing", "KeyUp: col=" + std::to_string(column) +
                    " eventTimeMs=" + std::to_string(eventTimeMs) +
                    " judgeTimeMs=" + std::to_string(judgeEventTimeMs));
    }
}

/// 处理 Tap/Hold 头部按键判定结果，更新分数与视觉反馈
void PlayingState::handlePressResult(gameplay::JudgmentResult result, int32_t column, int32_t row,
                                     int64_t pressTime, int64_t noteTime, bool isTapNote) {
    switch (result) {
    case gameplay::JudgmentResult::Perfect:
        m_perfectCount++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Perfect, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Perfect);
        m_audio.playSfx(audio::SfxType::HitNormal);
        break;
    case gameplay::JudgmentResult::Good:
        m_goodCount++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Good, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Good);
        m_audio.playSfx(audio::SfxType::HitNormal);
        break;
    case gameplay::JudgmentResult::Miss:
        m_missCount++;
        m_comboManager.onMiss();
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        break;
    case gameplay::JudgmentResult::Ignored:
        return;
    }

    m_lastHitDebug.judgeMs = pressTime;
    m_lastHitDebug.noteMs = noteTime;
    m_lastHitDebug.timing = pressTime - noteTime;
    m_lastHitDebug.result = result;

    if (isTapNote &&
        (result == gameplay::JudgmentResult::Perfect ||
         result == gameplay::JudgmentResult::Good)) {
        m_hitEffects.push_back({ column, row, 1.0f });
    }

    // 偏移条：记录判定时机
    if (m_offsetBarEnabled && result != gameplay::JudgmentResult::Ignored) {
        OffsetBarMark mark;
        mark.hitTime = pressTime;
        mark.noteTime = noteTime;
        mark.timing = pressTime - noteTime;
        mark.timer = OffsetBarMark::DURATION;
        mark.result = result;
        m_offsetBarMarks.push_back(mark);
    }

    // 添加判定弹出
    m_popups.push_back({column, result, JudgePopup::DURATION});
}

/// 处理 Hold 尾部释放判定结果
void PlayingState::handleHoldTailEvent(const gameplay::HoldTailEvent& evt) {
    const int32_t column = evt.col;
    switch (evt.result) {
    case gameplay::HoldReleaseResult::Perfect:
        m_perfectCount++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Perfect, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Perfect);
        m_popups.push_back({column, gameplay::JudgmentResult::Perfect, JudgePopup::DURATION});
        break;
    case gameplay::HoldReleaseResult::Good:
        m_goodCount++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Good, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Good);
        m_popups.push_back({column, gameplay::JudgmentResult::Good, JudgePopup::DURATION});
        break;
    case gameplay::HoldReleaseResult::Miss:
        m_missCount++;
        m_hitNotes++;
        m_comboManager.onMiss();
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        m_popups.push_back({column, gameplay::JudgmentResult::Miss, JudgePopup::DURATION});
        break;
    case gameplay::HoldReleaseResult::Ignored:
        return;
    }

    if (m_offsetBarEnabled) {
        OffsetBarMark mark;
        mark.hitTime = evt.releaseMs;
        mark.noteTime = evt.holdEndMs;
        mark.timing = evt.releaseMs - evt.holdEndMs;
        mark.timer = OffsetBarMark::DURATION;
        mark.result = evt.result == gameplay::HoldReleaseResult::Perfect
                          ? gameplay::JudgmentResult::Perfect
                      : evt.result == gameplay::HoldReleaseResult::Good
                          ? gameplay::JudgmentResult::Good
                          : gameplay::JudgmentResult::Miss;
        m_offsetBarMarks.push_back(mark);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染 HUD 与 ImGui 覆盖层
void PlayingState::render() {
    if (!m_gameplayInitialized) return;

    renderHUD();
    renderImGuiOverlay();
}

/// OpenGL HUD 渲染（预留，当前由 Renderer 负责主画面）
void PlayingState::renderHUD() {
    // [TODO] OpenGL HUD rendering
}

/// 渲染 ImGui 覆盖层：前导倒计时、分数/连击、偏移条、调试 HUD
void PlayingState::renderImGuiOverlay() {
    using namespace ui;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoInputs;

    // ── 前导倒计时 ──
    if (m_leadInActive) {
        auto& kernel = Kernel::instance();
        int64_t nowMs = kernel.clock().interpolatedNowMs();
        int64_t gameplayStartMs = m_firstNoteTimeMs - LEAD_IN_BEFORE_NOTE_MS;
        int64_t remainingMs = gameplayStartMs - nowMs;

        if (remainingMs > 0) {
            int countdown = static_cast<int>(remainingMs / 1000) + 1;
            if (countdown < 1) countdown = 1;

            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 60,
                                            ImGui::GetIO().DisplaySize.y / 2 - 40));
            ImGui::SetNextWindowSize(ImVec2(120, 80));
            ImGui::Begin("##LeadIn", nullptr, flags);
            ImGui::SetWindowFontScale(3.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
            ImGui::Text("%d", countdown);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);
            ImGui::End();

            // Skip 提示（仅当第一个note距离歌曲开始超过5秒时显示）
            if (m_firstNoteTimeMs > 5000) {
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 120,
                                                ImGui::GetIO().DisplaySize.y / 2 + 50));
                ImGui::SetNextWindowSize(ImVec2(240, 40));
                ImGui::Begin("##SkipHint", nullptr, flags);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 0.7f));
                ImGui::Text("SPACE to skip");
                ImGui::PopStyleColor();
                ImGui::End();
            }
        }
        return;
    }

    // ── Debug HUD（Settings 可开关）──
    if (m_debugHudEnabled) {
        auto& kernel = Kernel::instance();
        const int64_t songNowMs = kernel.clock().interpolatedNowMs();
        const int64_t visualLeadMs = platform::Config::getInt(platform::Config::KEY_VISUAL_LEAD, 16);
        const int64_t audioCursorMs = m_audio.positionMs();

        int64_t nextFormationTime = m_formationCtrl.nextFormationTime();
        int64_t nextScrollTime = INT64_MAX;
        if (m_formationCtrl.currentCols() > KEY_COUNT) {
            int32_t windowStart = m_scrollWindow.startCol;
            int32_t windowEnd = m_scrollWindow.endCol;
            for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
                const auto& colQ = m_judgeQueue.columnQueue(col);
                if (colQ.finished()) continue;
                const auto& note = colQ.front();
                if (note.time > songNowMs && (col < windowStart || col > windowEnd)) {
                    if (note.time < nextScrollTime) nextScrollTime = note.time;
                }
            }
        }

        const char* resultLabel = "?";
        switch (m_lastHitDebug.result) {
        case gameplay::JudgmentResult::Perfect: resultLabel = "Perfect"; break;
        case gameplay::JudgmentResult::Good:    resultLabel = "Good"; break;
        case gameplay::JudgmentResult::Miss:     resultLabel = "Miss"; break;
        default: break;
        }

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 340, 12));
        ImGui::SetNextWindowSize(ImVec2(680, 118));
        ImGui::Begin("##DebugHUD", nullptr, flags);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
        ImGui::Text("SONG %lld ms | AUDIO %lld ms | DRIFT %+lld ms",
                    songNowMs, audioCursorMs, songNowMs - audioCursorMs);
        ImGui::Text("TIMING OFF %+lld ms | VIS LEAD %lld ms | SCROLL %s | FORM next %lld",
                    m_timingOffsetMs, visualLeadMs,
                    m_scrollWindow.scrolling ? "YES" : "no",
                    nextFormationTime == INT64_MAX ? -1LL : nextFormationTime);
        ImGui::Text("LAST HIT %s | judge %lld | note %lld | timing %+lld ms",
                    resultLabel,
                    m_lastHitDebug.judgeMs,
                    m_lastHitDebug.noteMs,
                    m_lastHitDebug.timing);
        ImGui::PopStyleColor();
        ImGui::End();
    }

    // ── Top-left: Score ──
    ImGui::SetNextWindowPos(ImVec2(20, m_debugHudEnabled ? 140.0f : 80.0f));
    ImGui::SetNextWindowSize(ImVec2(300, 80));

    ImGui::Begin("##ScoreHUD", nullptr, flags);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::Text("%08d", static_cast<int>(m_scoreManager.totalScore()));
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    if (m_comboManager.current() > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
        ImGui::SetWindowFontScale(1.5f);
        ImGui::Text("%dx COMBO", m_comboManager.current());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }
    ImGui::End();

    // ── Top-right: Judgment counts ──
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 200, 20));
    ImGui::SetNextWindowSize(ImVec2(180, 80));

    ImGui::Begin("##JudgeHUD", nullptr, flags);
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::Text("Perfect: %d", m_perfectCount);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.702f, 1.0f, 0.0f, 1.0f));
    ImGui::Text("Good:    %d", m_goodCount);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    ImGui::Text("Miss:    %d", m_missCount);
    ImGui::PopStyleColor();
    ImGui::End();

    // ── Bottom: HP bar ──
    float barWidth = ImGui::GetIO().DisplaySize.x - 40;
    ImGui::SetNextWindowPos(ImVec2(20, ImGui::GetIO().DisplaySize.y - 50));
    ImGui::SetNextWindowSize(ImVec2(barWidth, 30));

    ImGui::Begin("##HPHUD", nullptr, flags);
    float hp = m_hpManager.hp();
    ImVec4 hpColor = hp > 0.5f ?
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.8f) :
        hp > 0.25f ? ImVec4(0.94f, 0.62f, 0.15f, 0.8f) :
                     ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.8f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
    ImGui::ProgressBar(hp, ImVec2(barWidth - 20, 20), "");
    ImGui::PopStyleColor();
    ImGui::End();

    // ── Offset Bar（偏移条）──
    if (m_offsetBarEnabled) {
        // 判定窗口公式与 StandardJudgeStrategy 保持一致（策略为 private，无法直接获取）
        float od = m_beatmap.difficulty.od;
        int32_t perfectW = static_cast<int32_t>(22.0f - 1.05f * od);
        int32_t goodW = static_cast<int32_t>(65.0f - 2.6f * od);
        if (perfectW < 1) perfectW = 1;
        if (goodW <= perfectW) goodW = perfectW + 1;

        const float obW = 400.0f;
        const float obH = 20.0f;
        const float scaleY = ImGui::GetIO().DisplaySize.y / renderer::GridLayout::kScreenH;
        const float matrixBottom = ImGui::GetIO().DisplaySize.y
            - renderer::GridLayout::kMargin * scaleY
            + renderer::GridLayout::kMatrixShiftY * scaleY;
        float obX = (ImGui::GetIO().DisplaySize.x - obW) * 0.5f;
        const float keyHintY = std::min(matrixBottom + 8.0f,
                                        ImGui::GetIO().DisplaySize.y - 58.0f - 44.0f);
        float obY = keyHintY - obH - 10.0f;
        float centerX = obX + obW * 0.5f;
        float perfectHalfW = (static_cast<float>(perfectW) / static_cast<float>(goodW)) * (obW * 0.5f);

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // 背景灰色（最外侧，超出 good 窗口）
        dl->AddRectFilled(ImVec2(obX, obY), ImVec2(obX + obW, obY + obH),
                          IM_COL32(60, 60, 70, 200));

        // Good 区域（绿色）
        ImU32 goodColor = IM_COL32(100, 200, 50, 200);
        dl->AddRectFilled(ImVec2(obX, obY),
                          ImVec2(centerX - perfectHalfW, obY + obH), goodColor);
        dl->AddRectFilled(ImVec2(centerX + perfectHalfW, obY),
                          ImVec2(obX + obW, obY + obH), goodColor);

        // Perfect 区域（青色）
        dl->AddRectFilled(ImVec2(centerX - perfectHalfW, obY),
                          ImVec2(centerX + perfectHalfW, obY + obH),
                          IM_COL32(0, 255, 245, 200));

        // 中心线（0ms 位置）白色
        dl->AddLine(ImVec2(centerX, obY - 2),
                    ImVec2(centerX, obY + obH + 2),
                    IM_COL32(255, 255, 255, 255), 2.0f);

        // 边框
        dl->AddRect(ImVec2(obX, obY), ImVec2(obX + obW, obY + obH),
                    IM_COL32(200, 200, 220, 220), 0.0f, 0, 1.0f);

        // 标记点（每次击中 note 时记录的 timing）
        for (const auto& mark : m_offsetBarMarks) {
            // 将 timing 限制在 ±goodW 范围内用于显示
            int64_t clamped = mark.timing;
            if (clamped < -static_cast<int64_t>(goodW)) clamped = -goodW;
            if (clamped >  static_cast<int64_t>(goodW)) clamped =  goodW;
            float markX = centerX + (static_cast<float>(clamped) / static_cast<float>(goodW)) * (obW * 0.5f);
            float alpha = std::min(1.0f, mark.timer / OffsetBarMark::DURATION);

            ImU32 markColor;
            int alphaByte = static_cast<int>(255.0f * alpha);
            switch (mark.result) {
            case gameplay::JudgmentResult::Perfect:
                markColor = IM_COL32(0, 255, 245, alphaByte);
                break;
            case gameplay::JudgmentResult::Good:
                markColor = IM_COL32(100, 200, 50, alphaByte);
                break;
            case gameplay::JudgmentResult::Miss:
                markColor = IM_COL32(255, 0, 110, alphaByte);
                break;
            default:
                markColor = IM_COL32(200, 200, 200, alphaByte);
                break;
            }

            // 标记为竖线
            dl->AddLine(ImVec2(markX, obY - 4),
                        ImVec2(markX, obY + obH + 4),
                        markColor, 3.0f);
        }
    }

    // ── Judgment popups ──
    int popupIdx = 0;
    for (const auto& popup : m_popups) {
        float alpha = std::min(1.0f, popup.timer / (JudgePopup::DURATION * 0.3f));
        float offsetY = (1.0f - popup.timer / JudgePopup::DURATION) * 40.0f;

        // 根据列号计算水平位置（4列活跃窗口居中，与 note_renderer 的 cellX 公式对齐）
        float colX = ImGui::GetIO().DisplaySize.x / 2;
        {
            const float W = ImGui::GetIO().DisplaySize.x;
            const float gw = renderer::GridLayout::kDefaultCellW *
                (W / renderer::GridLayout::kScreenW);
            const int32_t startCol = m_scrollWindow.startCol;
            // 动态偏移：活跃窗口宽度算中心偏移（不再硬编码1.5）
            int32_t activeWidth = m_scrollWindow.endCol - m_scrollWindow.startCol + 1;
            float noteCenterOffset = (activeWidth - 1) * 0.5f;
            colX = W * 0.5f + (popup.column - startCol - noteCenterOffset) * gw;
        }

        ImGui::SetNextWindowPos(ImVec2(colX - 50, ImGui::GetIO().DisplaySize.y / 2 - 80 - offsetY));
        ImGui::SetNextWindowSize(ImVec2(100, 40));
        char windowId[32];
        snprintf(windowId, sizeof(windowId), "##JudgePopup%d", popupIdx++);
        ImGui::Begin(windowId, nullptr, flags);

        const char* text = "";
        ImVec4 color(1, 1, 1, alpha);
        switch (popup.result) {
        case gameplay::JudgmentResult::Perfect:
            text = "PERFECT";
            color = ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, alpha);
            break;
        case gameplay::JudgmentResult::Good:
            text = "GOOD";
            color = ImVec4(0.702f, 1.0f, 0.0f, alpha);
            break;
        case gameplay::JudgmentResult::Miss:
            text = "MISS";
            color = ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, alpha);
            break;
        default:
            break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Text("%s", text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::End();
    }

    // ── 按键列高亮：类似节奏医生的判定区反馈，按住时整列发亮 ──
    {
        auto mapping = getKeyMapping();
        const float W = ImGui::GetIO().DisplaySize.x;
        const float H = ImGui::GetIO().DisplaySize.y;
        const float scaleX = W / renderer::GridLayout::kScreenW;
        const float scaleY = H / renderer::GridLayout::kScreenH;
        const float margin = renderer::GridLayout::kMargin * scaleY;
        const float shiftY = renderer::GridLayout::kMatrixShiftY * scaleY;
        const float gw = renderer::GridLayout::kDefaultCellW * scaleX;
        const float gh = renderer::GridLayout::kDefaultCellH * scaleY;
        const int32_t totalRows = m_formationCtrl.formationCount() > 0
            ? m_formationCtrl.current().rows
            : 4;
        const int32_t startCol = m_scrollWindow.startCol;
        int32_t activeWidth = m_scrollWindow.endCol - m_scrollWindow.startCol + 1;
        float noteCenterOffset = (activeWidth - 1) * 0.5f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        for (const auto& m : mapping) {
            bool pressed = false;
            for (int k = 0; k < KEY_COUNT; ++k) {
                if (m.sdlKey == KEY_CODES[k] && isKeyVisuallyDown(k)) {
                    pressed = true;
                    break;
                }
            }
            if (!pressed) continue;

            float cellX = W * 0.5f + (m.column - startCol - noteCenterOffset) * gw;
            float left = cellX - gw * 0.5f;
            float right = cellX + gw * 0.5f;
            float bottom = H - margin + shiftY;
            float top = std::max(0.0f, bottom - std::max(totalRows, 1) * gh);

            dl->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                              IM_COL32(0, 255, 245, 42), 10.0f);
            dl->AddRect(ImVec2(left + 3, top + 3), ImVec2(right - 3, bottom - 3),
                        IM_COL32(0, 255, 245, 150), 10.0f, 0, 3.0f);
            dl->AddLine(ImVec2(cellX, top + 8), ImVec2(cellX, bottom - 8),
                        IM_COL32(255, 255, 255, 110), 2.0f);
        }
    }

    // ── 按键提示：在对应列底部显示按键标签 ──
    {
        auto mapping = getKeyMapping();

        // 计算网格参数（与 Renderer 一致）
        // 按键提示固定在屏幕中央活跃列位置（不随滚动偏移），与 note_renderer 的 cellX 公式对齐
        const float W = ImGui::GetIO().DisplaySize.x;
        const float H = ImGui::GetIO().DisplaySize.y;
        const float scaleY = H / renderer::GridLayout::kScreenH;
        const float margin = renderer::GridLayout::kMargin * scaleY;
        const float shiftY = renderer::GridLayout::kMatrixShiftY * scaleY;
        const float gw = renderer::GridLayout::kDefaultCellW *
            (W / renderer::GridLayout::kScreenW);
        const float matrixBottom = H - margin + shiftY;
        const float keyH = 44.0f;
        const float keyHintY = std::min(matrixBottom + 8.0f, H - 58.0f - keyH);
        const int32_t startCol = m_scrollWindow.startCol;
        // 动态偏移：活跃窗口宽度算中心偏移（不再硬编码1.5）
        int32_t activeWidth = m_scrollWindow.endCol - m_scrollWindow.startCol + 1;
        float noteCenterOffset = (activeWidth - 1) * 0.5f;

        for (const auto& m : mapping) {
            // 按键固定在屏幕中央活跃列：col=startCol+0..(activeWidth-1) 居中
            float cellX = W * 0.5f + (m.column - startCol - noteCenterOffset) * gw;
            float keyW = std::min(gw * 0.8f, 80.0f);
            // 查找该列对应的按键标签
            const char* label = "?";
            for (int k = 0; k < KEY_COUNT; ++k) {
                if (m.sdlKey == KEY_CODES[k]) {
                    label = KEY_LABELS[k];
                    break;
                }
            }

            // 检查该键是否按下
            bool pressed = false;
            for (int k = 0; k < KEY_COUNT; ++k) {
                if (m.sdlKey == KEY_CODES[k] && isKeyVisuallyDown(k)) {
                    pressed = true;
                    break;
                }
            }

            char windowId[32];
            snprintf(windowId, sizeof(windowId), "##KeyHint%d", m.column);
            ImGui::SetNextWindowPos(ImVec2(cellX - keyW / 2, keyHintY));
            ImGui::SetNextWindowSize(ImVec2(keyW, keyH));

            ImGui::Begin(windowId, nullptr, flags);

            // 绘制按键背景
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            ImU32 bgColor = pressed ? IM_COL32(0, 255, 245, 190) : IM_COL32(20, 20, 40, 160);
            ImU32 borderColor = pressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(50, 50, 70, 180);
            dl->AddRectFilled(ImVec2(wp.x + 2, wp.y + 2), ImVec2(wp.x + keyW - 2, wp.y + keyH - 2), bgColor, 6.0f);
            dl->AddRect(ImVec2(wp.x + 2, wp.y + 2), ImVec2(wp.x + keyW - 2, wp.y + keyH - 2),
                        borderColor, 6.0f, 0, pressed ? 4.0f : 1.0f);

            // 按键文字
            ImU32 textColor = pressed ? IM_COL32(10, 20, 35, 255) : IM_COL32(140, 140, 170, 220);
            ImGui::SetWindowFontScale(1.6f);
            float textWidth = ImGui::CalcTextSize(label).x;
            dl->AddText(ImVec2(wp.x + keyW / 2 - textWidth / 2, wp.y + 12), textColor, label);

            // ── 锁判定视觉提示：滚动/非 SCALE_ONLY 变换期间显示红色叉号 ──
            const bool judgmentLocked = m_scrollWindow.scrolling ||
                isFormationJudgmentBlocked(Kernel::instance().clock().interpolatedNowMs());
            if (judgmentLocked) {
                ImU32 xColor = IM_COL32(255, 40, 60, 255);
                const float xPad = 6.0f;
                const float xThick = 3.0f;
                dl->AddLine(ImVec2(wp.x + xPad, wp.y + xPad),
                            ImVec2(wp.x + keyW - xPad, wp.y + keyH - xPad),
                            xColor, xThick);
                dl->AddLine(ImVec2(wp.x + keyW - xPad, wp.y + xPad),
                            ImVec2(wp.x + xPad, wp.y + keyH - xPad),
                            xColor, xThick);
            }

            ImGui::SetWindowFontScale(1.0f);
            ImGui::End();
        }
    }

} // renderImGuiOverlay()

} // namespace melody_matrix::core

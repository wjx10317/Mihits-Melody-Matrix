#include "playing_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/result_state.h"
#include "platform/config.h"
#include "ui/theme.h"
#include "util/logger.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/beatmap_builder.h"
#include <algorithm>
#include <SDL.h>
#include "platform/file_system.h"

#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace melody_matrix::core {

constexpr int PlayingState::KEY_CODES[];

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

        MM_LOG_INFO("Playing", "Resuming playback");
        return;
    }

    initGameplay();
}

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

void PlayingState::cleanupRenderer() {
    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);
    renderer.setBackgroundPath("");
}

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
    m_popups.clear();

    m_scrollWindow = {};  // 重置滚动窗口
    m_completedNotes.clear();  // 清空已完成区域

    m_autoplay = false;
    m_offsetBarEnabled = false;
    m_offsetBarMarks.clear();

    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);
    renderer.setBackgroundPath("");

    MM_LOG_INFO("Playing", "Gameplay reset complete");
}

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

    // ── 铺面导入丢弃：丢弃会在滚动过程中变得不可击打的 note ──
    // 当列数 > KEY_COUNT 时，滚动期间锁判定。
    // 如果一个 note 在滚动期间到达 miss 阈值，或处于离开窗口的列中，
    // 玩家/autoplay 无法击打却被判 miss，不公平。在导入时模拟滚动触发，丢弃这些 note。
    if (m_beatmap.formations.size() > 0) {
        int32_t maxCols = 0;
        for (const auto& f : m_beatmap.formations) {
            if (f.cols > maxCols) maxCols = f.cols;
        }
        if (maxCols > KEY_COUNT) {
            float od = m_beatmap.difficulty.od;
            int32_t goodW = static_cast<int32_t>(65.0f - 2.6f * od);
            int64_t missThr = static_cast<int64_t>(goodW) + 50;
            float approachMs = 1800.0f - m_beatmap.difficulty.ar * 120.0f;
            if (approachMs < 300.0f) approachMs = 300.0f;
            // 使用实际最大滚动时长（与 checkAndTriggerScroll 的上限一致）
            int64_t scrollDuration = 200;

            // 按时间排序，确保模拟与实际运行时一致
            std::vector<beatmap::Note> sortedNotes = m_beatmap.notes;
            std::sort(sortedNotes.begin(), sortedNotes.end(),
                      [](const beatmap::Note& a, const beatmap::Note& b) { return a.time < b.time; });

            std::vector<bool> discard(sortedNotes.size(), false);

            int32_t currentStart = 0;
            int32_t currentEnd = KEY_COUNT - 1;
            int64_t lastScrollEnd = 0;

            for (size_t i = 0; i < sortedNotes.size(); ++i) {
                if (discard[i]) continue;
                const auto& note = sortedNotes[i];

                // 检查此 note 是否需要滚动（col 超出当前窗口）
                if (note.col > currentEnd || note.col < currentStart) {
                    int32_t newStart = currentStart;
                    if (note.col > currentEnd) {
                        newStart = note.col - KEY_COUNT + 1;
                    } else {
                        newStart = note.col;
                    }
                    if (newStart < 0) newStart = 0;
                    if (newStart + KEY_COUNT > maxCols) newStart = maxCols - KEY_COUNT;

                    int64_t scrollStart = note.time - static_cast<int64_t>(approachMs);
                    if (scrollStart < lastScrollEnd) scrollStart = lastScrollEnd;
                    int64_t scrollEnd = scrollStart + scrollDuration;

                    // 检查触发滚动的 note 是否会在滚动期间 miss
                    if (scrollEnd >= note.time + missThr) {
                        discard[i] = true;
                        MM_LOG_INFO("Playing", "Discarding note at t=" + std::to_string(note.time) +
                                    " col=" + std::to_string(note.col) + " (would miss during scroll)");
                        continue;
                    }

                    // 检查离开窗口列中尚未击打的 note 是否会变得不可击打
                    int32_t leaveStart = -1, leaveEnd = -1;
                    if (newStart > currentStart) {
                        // 右滚：[currentStart, newStart-1] 离开窗口
                        leaveStart = currentStart;
                        leaveEnd = newStart - 1;
                    } else if (newStart < currentStart) {
                        // 左滚：[newStart+KEY_COUNT, currentEnd] 离开窗口
                        leaveStart = newStart + KEY_COUNT;
                        leaveEnd = currentEnd;
                    }

                    if (leaveStart >= 0) {
                        for (size_t j = i + 1; j < sortedNotes.size(); ++j) {
                            if (discard[j]) continue;
                            const auto& n = sortedNotes[j];
                            if (n.col >= leaveStart && n.col <= leaveEnd) {
                                // 该 note 在离开窗口的列中
                                // 若其时间在滚动开始之后，则无法在滚动前击打，
                                // 滚动期间锁判定，滚动后该列已不在窗口内 → 不可击打
                                if (n.time > scrollStart) {
                                    discard[j] = true;
                                    MM_LOG_INFO("Playing", "Discarding note at t=" + std::to_string(n.time) +
                                                " col=" + std::to_string(n.col) +
                                                " (in leaving column during scroll)");
                                }
                            }
                        }
                    }

                    currentStart = newStart;
                    currentEnd = newStart + KEY_COUNT - 1;
                    lastScrollEnd = scrollEnd;
                }
            }

            // 执行过滤
            std::vector<beatmap::Note> filteredNotes;
            filteredNotes.reserve(sortedNotes.size());
            size_t discarded = 0;
            for (size_t i = 0; i < sortedNotes.size(); ++i) {
                if (discard[i]) {
                    ++discarded;
                } else {
                    filteredNotes.push_back(sortedNotes[i]);
                }
            }

            if (discarded > 0) {
                MM_LOG_INFO("Playing", "Discarded " + std::to_string(discarded) +
                            " notes that would be unplayable during scroll");
                m_beatmap.notes = std::move(filteredNotes);
            }
        }
    }

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
    renderer.setGameplayRendering(true);

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
        renderer.setFormation(f.rows, f.cols);
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

GameState PlayingState::update(float dt) {
    if (!m_gameplayInitialized) return GameState::Count;

    // ── Sync clock from audio ──
    auto& kernel = Kernel::instance();
    kernel.clock().syncFromAudio(m_audio.positionMs());

    int64_t nowMs = kernel.clock().interpolatedNowMs();
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
    // 滚动期间锁判定：既不处理输入，也不触发 auto-miss，
    // 避免对正在 move 的列产生误判或悬空引用。
    bool inTransition = m_formationCtrl.inTransition(nowMs);
    bool inScroll = m_scrollWindow.scrolling;
    if (!inTransition && !inScroll) {
        // Autoplay 模组：自动击打当前滚动窗口内的列
        if (m_autoplay) {
            int32_t startCol = m_scrollWindow.startCol;
            int32_t endCol = m_scrollWindow.endCol;
            for (int32_t c = startCol; c <= endCol; ++c) {
                if (c < 0 || c >= m_judgeQueue.columnCount()) continue;

                // 优先处理活跃 Hold 的自动释放
                const auto* activeHold = m_judgeQueue.getActiveHold(c);
                if (activeHold && nowMs >= activeHold->holdEnd) {
                    // Autoplay：以 holdEnd 作为 releaseTime，保证 dt=0 落在 perfect 窗口内
                    auto holdResult = m_judgeQueue.onKeyRelease(activeHold->holdEnd, c, od);
                    handleHoldReleaseResult(holdResult);
                    continue;
                }
                if (activeHold) continue;  // 正在按住，等待释放

                const auto& colQ = m_judgeQueue.columnQueue(c);
                if (colQ.finished()) continue;
                const auto& note = colQ.front();

                // 自动击打：当 note 到达判定正中心
                if (nowMs >= note.time) {
                    // Autoplay：以 note.time 作为 pressTime，保证 dt=0 必在 perfect 窗口内
                    int64_t noteTime = note.time;
                    auto result = m_judgeQueue.onKeyPress(noteTime, c, od);
                    if (result != gameplay::JudgmentResult::Ignored) {
                        // pressTime 传 noteTime 使偏移条显示 timing=0（完美中心）
                        handlePressResult(result, c, noteTime, noteTime);
                    }
                }
            }
        }
        processInput();
        m_judgeQueue.update(nowMs, od);
    }

    // ── 列滚动逻辑 ──
    // 检查滚动是否完成
    if (inScroll && m_scrollWindow.finished(nowMs)) {
        completeScroll();
    }
    // 检查是否需要触发滚动
    if (!inScroll) {
        checkAndTriggerScroll(nowMs);
    }
    // 同步滚动状态和判定头到渲染器
    float scrollOffset = 0.0f;
    if (m_scrollWindow.scrolling) {
        float p = m_scrollWindow.progress(nowMs);
        // ease-in-out 缓动
        float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;
        int32_t colDelta = m_scrollWindow.targetStartCol - m_scrollWindow.startCol;
        const float W = 1920.0f, margin = 120.0f;
        int32_t totalCols = m_formationCtrl.currentCols();
        if (totalCols <= 0) totalCols = 4;
        float gw = (W - 2 * margin) / totalCols;
        scrollOffset = colDelta * gw * easedP;
    }
    kernel.renderer().setScrollState(m_scrollWindow.startCol, m_scrollWindow.endCol, scrollOffset,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetStartCol : m_scrollWindow.startCol,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetEndCol : m_scrollWindow.endCol,
                                      m_scrollWindow.scrolling, m_scrollWindow.progress(nowMs));
    {
        std::array<size_t, 8> heads = {};
        for (int32_t c = 0; c < m_judgeQueue.columnCount() && c < 8; ++c) {
            heads[c] = m_judgeQueue.columnQueue(c).head;
        }
        kernel.renderer().setColumnHeads(heads, m_judgeQueue.columnCount());
    }

    // ── Update formation ──
    bool formationChanged = m_formationCtrl.update(nowMs);
    if (formationChanged) {
        const auto& prev = m_formationCtrl.formationAt(
            m_formationCtrl.currentIndex() > 0 ? m_formationCtrl.currentIndex() - 1 : 0);
        const auto& next = m_formationCtrl.current();
        int64_t durationMs = next.transitionDurationMs;
        if (durationMs > 0) {
            // 启动过渡动画
            m_formationCtrl.setTransitionDuration(durationMs);
            kernel.renderer().beginFormationTransition(prev.rows, prev.cols, next.rows, next.cols);
        } else {
            // 瞬间切换
            kernel.renderer().setFormation(next.rows, next.cols);
        }

        // 阵型变化时重置滚动窗口
        int32_t newCols = next.cols;
        if (newCols > KEY_COUNT) {
            m_scrollWindow.startCol = 0;
            m_scrollWindow.endCol = KEY_COUNT - 1;
        } else {
            m_scrollWindow.startCol = 0;
            m_scrollWindow.endCol = newCols - 1;
        }
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

    return GameState::Count;
}

std::vector<PlayingState::KeyColumnMapping> PlayingState::getKeyMapping() const {
    int32_t totalCols = m_formationCtrl.currentCols();
    if (totalCols <= 0) totalCols = 4;

    std::vector<KeyColumnMapping> mapping;

    if (totalCols <= KEY_COUNT) {
        // 总列数 <= 4：按键直接映射到对应列
        // D=col0, F=col1, J=col2, K=col3（只映射存在的列）
        if (totalCols >= 1) mapping.push_back({SDLK_d, 0});
        if (totalCols >= 2) mapping.push_back({SDLK_f, 1});
        if (totalCols >= 3) mapping.push_back({SDLK_j, 2});
        if (totalCols >= 4) mapping.push_back({SDLK_k, 3});
    } else {
        // 总列数 > KEY_COUNT：使用滚动窗口映射
        // D F J K 映射到 startCol ~ startCol+3
        int32_t startCol = m_scrollWindow.startCol;
        mapping.push_back({SDLK_d, startCol + 0});
        mapping.push_back({SDLK_f, startCol + 1});
        mapping.push_back({SDLK_j, startCol + 2});
        mapping.push_back({SDLK_k, startCol + 3});
    }

    return mapping;
}

void PlayingState::checkAndTriggerScroll(int64_t nowMs) {
    int32_t totalCols = m_formationCtrl.currentCols();
    if (totalCols <= KEY_COUNT) return;  // 不需要滚动

    float approachMs = 1800.0f - m_beatmap.difficulty.ar * 120.0f;
    if (approachMs < 300.0f) approachMs = 300.0f;

    int32_t windowStart = m_scrollWindow.startCol;
    int32_t windowEnd = m_scrollWindow.endCol;

    // 遍历 JudgeQueue 各列，查找 approach 窗口内最早需要判定的音符
    int32_t neededStart = windowStart;
    int32_t neededEnd = windowEnd;

    for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
        const auto& colQ = m_judgeQueue.columnQueue(col);
        if (colQ.finished()) continue;

        const auto& note = colQ.front();
        float timeDiff = static_cast<float>(note.time - nowMs);

        // 音符在 approach 窗口内（即将需要判定）
        // 仅对未过期 note（timeDiff > 0）触发滚动，避免对已过期 note 触发
        if (timeDiff <= approachMs && timeDiff > 0) {
            if (col < neededStart) neededStart = col;
            if (col > neededEnd) neededEnd = col;
        }
    }

    // 如果需要的列超出当前窗口，触发滚动
    if (neededStart < windowStart || neededEnd > windowEnd) {
        // 计算新的窗口起始列
        int32_t targetStart = windowStart;

        // 向右滚动：neededEnd 超出窗口右边界（如 col=4 出现 note，窗口 0-3 → 1-4）
        if (neededEnd > windowEnd) {
            targetStart = neededEnd - KEY_COUNT + 1;
        }
        // 向左滚动：neededStart 超出窗口左边界
        // （仅在不需要向右滚动时处理；时间向前流动，优先保证新列进入窗口）
        if (neededStart < windowStart && neededEnd <= windowEnd) {
            targetStart = neededStart;
        }

        // 边界约束：窗口不能超出总列数范围
        if (targetStart < 0) targetStart = 0;
        if (targetStart + KEY_COUNT > totalCols) {
            targetStart = totalCols - KEY_COUNT;
        }

        // 只在窗口确实需要移动时才触发
        if (targetStart != windowStart) {
            // ── 自适应滚动时长 ──
            // 找到触发滚动的 note 的剩余时间，确保滚动完成后仍有足够击打窗口
            // 击打窗口 = goodWindow + 10ms（比 good 快区间再快 10ms）
            float od = m_beatmap.difficulty.od;
            int32_t goodW = static_cast<int32_t>(65.0f - 2.6f * od);
            int64_t minRemainingMs = static_cast<int64_t>(goodW) + 10;  // 最小剩余时间

            // 找到最早需要判定的 note 的时间
            int64_t earliestNoteTime = INT64_MAX;
            for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
                const auto& colQ = m_judgeQueue.columnQueue(col);
                if (colQ.finished()) continue;
                if (col < targetStart || col > targetStart + KEY_COUNT - 1) continue;
                if (colQ.front().time > nowMs && colQ.front().time < earliestNoteTime) {
                    earliestNoteTime = colQ.front().time;
                }
            }

            // 默认滚动时长 200ms
            float scrollDuration = 200.0f;
            if (earliestNoteTime != INT64_MAX) {
                int64_t availableTime = earliestNoteTime - nowMs - minRemainingMs;
                // 滚动时长 = 可用时间的 80%，但不超过 200ms，不小于 50ms
                scrollDuration = static_cast<float>(availableTime) * 0.8f;
                scrollDuration = std::max(50.0f, std::min(200.0f, scrollDuration));
            }

            m_scrollWindow.scrolling = true;
            m_scrollWindow.scrollStartMs = nowMs;
            m_scrollWindow.scrollDurationMs = scrollDuration;
            m_scrollWindow.targetStartCol = targetStart;
            m_scrollWindow.targetEndCol = targetStart + KEY_COUNT - 1;

            MM_LOG_INFO("Playing", "Scroll triggered: " +
                        std::to_string(windowStart) + "-" + std::to_string(windowEnd) +
                        " -> " + std::to_string(targetStart) + "-" +
                        std::to_string(targetStart + KEY_COUNT - 1) +
                        " at t=" + std::to_string(nowMs) +
                        "ms duration=" + std::to_string(static_cast<int>(scrollDuration)) + "ms");
        }
    }
}

void PlayingState::completeScroll() {
    int32_t oldStart = m_scrollWindow.startCol;
    int32_t oldEnd = m_scrollWindow.endCol;
    int32_t newStart = m_scrollWindow.targetStartCol;
    int32_t newEnd = m_scrollWindow.targetEndCol;

    // ── move 语义：将离开窗口的列的 note 数据转移到已完成区域 ──
    // 环形队列管理：JudgeQueue 的固定 MAX_COLS 列数组作为环形缓冲，
    // 滚动窗口在其上滑动；离开窗口的列被"回收"——note 数据 move 到
    // m_completedNotes，保证 note 不丢失。滚动期间锁判定，move 不产生歧义。
    if (newStart > oldStart) {
        // 向右滚动：左侧列 [oldStart, newStart-1] 离开窗口
        // guard：仅 move 已完成列；未完成列保留在 JudgeQueue 中，
        // 滚动完成后由 update() 的 auto-miss 机制处理，避免 note 静默丢失。
        for (int32_t col = oldStart; col < newStart; ++col) {
            if (!m_judgeQueue.columnQueue(col).finished()) {
                MM_LOG_WARN("Playing", "Right scroll encountered unjudged notes in col " +
                            std::to_string(col) + ", skip move (will auto-miss)");
                continue;
            }
            auto moved = m_judgeQueue.moveColumnNotes(col);
            if (!moved.empty()) {
                for (auto& n : moved) {
                    m_completedNotes.push_back(std::move(n));
                }
                MM_LOG_INFO("Playing", "Moved col " + std::to_string(col) +
                            " notes to completed area");
            }
        }
    } else if (newStart < oldStart) {
        // 向左滚动：右侧列 [newEnd+1, oldEnd] 离开窗口
        // 时间向前流动时正常不会向左滚动（右侧列的 note 应在窗口内已判定完成）。
        // 若因谱面列分布异常触发，未判定 note 不应被直接 move 到 completed 区域
        // （会导致漏判且不计 miss）。此处加 guard：仅 move 已完成列；未完成列保留
        // 在 JudgeQueue 中，滚动完成后由 update() 的 auto-miss 机制处理。
        for (int32_t col = newEnd + 1; col <= oldEnd; ++col) {
            if (!m_judgeQueue.columnQueue(col).finished()) {
                MM_LOG_WARN("Playing", "Left scroll encountered unjudged notes in col " +
                            std::to_string(col) + ", skip move (will auto-miss)");
                continue;
            }
            auto moved = m_judgeQueue.moveColumnNotes(col);
            if (!moved.empty()) {
                for (auto& n : moved) {
                    m_completedNotes.push_back(std::move(n));
                }
                MM_LOG_INFO("Playing", "Moved col " + std::to_string(col) +
                            " notes to completed area");
            }
        }
    }

    m_scrollWindow.startCol = newStart;
    m_scrollWindow.endCol = newEnd;
    m_scrollWindow.scrolling = false;

    MM_LOG_INFO("Playing", "Scroll completed: window now " +
                std::to_string(m_scrollWindow.startCol) + "-" +
                std::to_string(m_scrollWindow.endCol));
}

void PlayingState::processInput() {
    auto& kernel = Kernel::instance();
    int64_t nowMs = kernel.clock().interpolatedNowMs();
    float od = m_beatmap.difficulty.od;

    // 构建按键→列映射
    auto mapping = getKeyMapping();
    std::array<int32_t, KEY_COUNT> keyToCol;
    keyToCol.fill(-1);
    for (int k = 0; k < KEY_COUNT; ++k) {
        for (size_t i = 0; i < mapping.size(); ++i) {
            if (mapping[i].sdlKey == KEY_CODES[k]) {
                keyToCol[k] = mapping[i].column;
                break;
            }
        }
    }

    const auto& keyEvents = kernel.frameKeyEvents();

    for (const auto& evt : keyEvents) {
        // 跳过 ESC（由 Kernel 处理状态转换）
        if (evt.key == SDLK_ESCAPE) continue;

        // 查找按键对应的列
        int32_t column = -1;
        for (int k = 0; k < KEY_COUNT; ++k) {
            if (evt.key == KEY_CODES[k]) {
                column = keyToCol[k];
                // 更新 DEBUG 显示状态
                m_curKeyDown[k] = evt.pressed;
                break;
            }
        }
        if (column < 0) continue;  // 非游戏按键，跳过

        if (evt.pressed) {
            // 获取头部 note 的时间（用于偏移条记录），需在 onKeyPress 之前读取
            int64_t noteTime = 0;
            if (column >= 0 && column < m_judgeQueue.columnCount()) {
                const auto& colQ = m_judgeQueue.columnQueue(column);
                if (!colQ.finished()) {
                    noteTime = colQ.front().time;
                }
            }
            auto result = m_judgeQueue.onKeyPress(nowMs, column, od);
            MM_LOG_INFO("Playing", "KeyDown: col=" + std::to_string(column) +
                        " nowMs=" + std::to_string(nowMs) +
                        " result=" + std::to_string(static_cast<int>(result)));
            handlePressResult(result, column, nowMs, noteTime);
        } else {
            auto holdResult = m_judgeQueue.onKeyRelease(nowMs, column, od);
            MM_LOG_INFO("Playing", "KeyUp: col=" + std::to_string(column) +
                        " nowMs=" + std::to_string(nowMs) +
                        " result=" + std::to_string(static_cast<int>(holdResult)));
            handleHoldReleaseResult(holdResult);
        }
    }
}

void PlayingState::handlePressResult(gameplay::JudgmentResult result, int32_t column,
                                     int64_t pressTime, int64_t noteTime) {
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
        return;  // Ignored 不产生弹出
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

void PlayingState::handleHoldReleaseResult(gameplay::HoldReleaseResult result) {
    switch (result) {
    case gameplay::HoldReleaseResult::Perfect:
        m_scoreManager.addScore(gameplay::JudgmentResult::Perfect, m_comboManager.current());
        break;
    case gameplay::HoldReleaseResult::Good:
        m_scoreManager.addScore(gameplay::JudgmentResult::Good, m_comboManager.current());
        break;
    case gameplay::HoldReleaseResult::Miss:
        m_missCount++;
        m_hitNotes++;
        m_comboManager.onMiss();
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        break;
    case gameplay::HoldReleaseResult::Ignored:
        break;
    }
}

void PlayingState::render() {
    if (!m_gameplayInitialized) return;

    // 控制网格和音符渲染：矩阵不可见时不渲染
    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(m_matrixVisible);

    renderHUD();
    renderImGuiOverlay();
}

void PlayingState::renderHUD() {
    // [TODO] OpenGL HUD rendering
}

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

    // ── Top-left: Score ──
    ImGui::SetNextWindowPos(ImVec2(20, 20));
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
        float obX = (ImGui::GetIO().DisplaySize.x - obW) * 0.5f;
        // 上移至 -140，避免与暂停提示（-90）重叠
        float obY = ImGui::GetIO().DisplaySize.y - 140.0f;
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

        // 根据列号计算水平位置（使用完整矩阵列位置）
        float colX = ImGui::GetIO().DisplaySize.x / 2;
        int32_t totalCols = m_formationCtrl.currentCols();
        if (totalCols <= 0) totalCols = 4;
        {
            const float margin = 120.0f;
            const float gw = (ImGui::GetIO().DisplaySize.x - 2 * margin) / totalCols;
            colX = margin + (popup.column + 0.5f) * gw;
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

    // ── Center: Pause hint ──
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 100,
                                    ImGui::GetIO().DisplaySize.y - 90));
    ImGui::Begin("##PauseHint", nullptr, flags);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
    ImGui::Text("ESC to pause");
    ImGui::PopStyleColor();
    ImGui::End();

    // ── 按键提示：在对应列底部显示按键标签 ──
    {
        int32_t totalCols = m_formationCtrl.currentCols();
        if (totalCols <= 0) totalCols = 4;

        auto mapping = getKeyMapping();

        // 计算网格参数（与 Renderer 一致）
        const float W = ImGui::GetIO().DisplaySize.x;
        const float H = ImGui::GetIO().DisplaySize.y;
        const float margin = 120.0f;
        const float gw = (W - 2 * margin) / totalCols;
        const float keyHintY = H - margin + 15;  // 网格底部下方

        for (const auto& m : mapping) {
            float cellX = margin + (m.column + 0.5f) * gw;
            float keyW = std::min(gw * 0.8f, 80.0f);  // 按键宽度，最大80px
            float keyH = 44.0f;

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
                if (m.sdlKey == KEY_CODES[k] && m_curKeyDown[k]) {
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
            ImU32 bgColor = pressed ? IM_COL32(0, 255, 245, 120) : IM_COL32(20, 20, 40, 160);
            ImU32 borderColor = pressed ? IM_COL32(0, 255, 245, 220) : IM_COL32(50, 50, 70, 180);
            dl->AddRectFilled(ImVec2(wp.x + 2, wp.y + 2), ImVec2(wp.x + keyW - 2, wp.y + keyH - 2), bgColor, 6.0f);
            dl->AddRect(ImVec2(wp.x + 2, wp.y + 2), ImVec2(wp.x + keyW - 2, wp.y + keyH - 2), borderColor, 6.0f);

            // 按键文字
            ImU32 textColor = pressed ? IM_COL32(0, 255, 245, 255) : IM_COL32(140, 140, 170, 220);
            ImGui::SetWindowFontScale(1.6f);
            float textWidth = ImGui::CalcTextSize(label).x;
            dl->AddText(ImVec2(wp.x + keyW / 2 - textWidth / 2, wp.y + 12), textColor, label);

            // ── 锁判定视觉提示：滚动期间显示红色叉号 ──
            if (m_scrollWindow.scrolling) {
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

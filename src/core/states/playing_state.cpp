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
#include "audio/miniaudio_cursor_playhead.h"
#include "audio/wasapi_playhead.h"
#include "time/host_clock.h"
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

/// 进入游玩状态：首次 initGameplay 或从暂停/重试恢复
void PlayingState::onEnter() {
    MM_LOG_INFO("Playing", "Entering Playing state");

    SDL_StopTextInput();                                     // 禁用 IME，防止中文输入法拦截 KEYDOWN

    if (m_needsReinit && m_gameplayInitialized) {            // Retry/Quit 后需完整重置
        resetGameplay();
    }

    if (m_gameplayInitialized) {                               // 从 Paused 恢复（非首次进入）
        if (!m_songFinished) {                               // 歌曲已结束则不恢复音频
            m_audio.resume();
            Kernel::instance().clock().resume();
        }
        Kernel::instance().renderer().setGameplayRendering(true);

        m_curKeyDown = {};                                   // 清按键状态，防暂停期间按键残留
        m_autoKeyDown = {};
        m_autoKeyFlash = {};

        MM_LOG_INFO("Playing", "Resuming playback");
        return;                                              // 跳过 initGameplay
    }

    initGameplay();                                          // 首次进入：加载谱面并开播
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

/// 重置全部子系统以支持重试（保留谱面路径，不重新 setBeatmapFile）
void PlayingState::resetGameplay() {
    MM_LOG_INFO("Playing", "Resetting gameplay for retry");

    m_audio.stop();
    m_audio.shutdown();                                      // 释放音频资源

    Kernel::instance().clock().reset();                      // 时钟归零

    m_judgeQueue.reset();
    m_scoreManager.reset();
    m_comboManager.reset();
    m_hpManager.reset();
    m_formationCtrl.reset();                               // 各 gameplay 子系统清零

    m_gameplayInitialized = false;                           // 下次 onEnter 走 initGameplay
    m_songFinished = false;
    m_playerDied = false;
    m_totalNotes = 0;
    m_hitNotes = 0;
    m_hit300Count = 0;
    m_hit100Count = 0;
    m_hit50Count = 0;
    m_missCount = 0;
    m_needsReinit = false;
    m_leadInActive = false;
    m_matrixVisible = false;
    m_curKeyDown = {};
    m_autoKeyDown = {};
    m_autoKeyFlash = {};
    m_popups.clear();

    m_scrollWindow = {};
    m_lastTransitionEndMs = 0;

    m_autoplay = false;                                      // mod 在 initGameplay 中按 m_modIds 重设
    m_timingOffsetMs = 0;                                    // initGameplay 会从 Config 重读
    m_offsetBarEnabled = false;
    m_offsetBarMarks.clear();
    m_hitEffects.clear();
    m_lastHitDebug = {};
    m_playhead.reset();
    m_lastPlayheadSampleHostMs = 0;
    m_playheadWindowStats = {};
    m_cursorSmooth = {};

    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);
    renderer.setBackgroundPath("");                          // 清背景，避免重试时残留

    MM_LOG_INFO("Playing", "Gameplay reset complete");
}

/// 首次加载谱面：解析、初始化判定/分数/HP/变阵、启动音频（Playing 核心初始化）
void PlayingState::initGameplay() {
    if (m_gameplayInitialized) return;                       // 已初始化则直接返回（防重复）

    MM_LOG_INFO("Playing", "Initializing gameplay with: " + m_beatmapFile);

    // ── 初始化音频引擎 ──
    if (!m_audio.init()) {                                   // miniaudio 等后端初始化
        MM_LOG_ERROR("Playing", "Failed to initialize audio engine");
        return;                                              // 音频失败则中止（后续无法同步时钟）
    }
    m_audio.loadSfx();                                       // 预加载击打音效
    // Playhead 在 playSong 之后创建（见下方），以便 WASAPI epoch 对齐开播

    // ── 重置时钟 ──
    // 从 Result/Retry 回来时时钟可能处于暂停状态，必须重置
    Kernel::instance().clock().reset();                      // 歌曲时间归零，非暂停

    // ── 加载谱面：按扩展名选择 .mma / .osu 解析器 ──
    auto parser = beatmap::createParserForFile(m_beatmapFile);
    if (!parser) {                                           // 不支持的扩展名
        MM_LOG_ERROR("Playing", "No parser available for: " + m_beatmapFile);
        return;
    }

    std::ifstream ifs(m_beatmapFile);                        // 打开谱面文件
    if (!ifs.is_open()) {
        MM_LOG_ERROR("Playing", "Cannot open file: " + m_beatmapFile);
        return;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();                                       // 读入全文到内存
    std::string content = ss.str();

    beatmap::BeatmapBuilder builder;                           // 中间构建器
    auto parseResult = parser->parse(content, builder);      // 文本 → 结构化数据
    if (!parseResult.ok()) {
        MM_LOG_ERROR("Playing", "Failed to parse beatmap: " + parseResult.error().message);
        return;
    }

    auto buildResult = builder.build();                        // 校验并生成 Beatmap
    if (!buildResult.ok()) {
        MM_LOG_ERROR("Playing", "Beatmap validation failed: " + buildResult.error().message);
        return;
    }
    m_beatmap = std::move(buildResult.value());              // 保存到成员，供全状态使用

    // ── 初始化判定队列 ──
    m_judgeQueue.setStrategy(std::make_unique<gameplay::StableJudgeStrategy>());  // Stable 300/100/50/Miss
    m_judgeQueue.loadNotes(m_beatmap.notes);                 // 按列建立 note 队列

    // ── 连接 JudgeQueue 事件回调（Miss / Hold 尾等自动判定路径）──
    m_judgeQueue.onHit = [this](const gameplay::NoteHitEvent& evt) {
        MM_LOG_DEBUG("Playing", "Hit: col=" + std::to_string(evt.col) +
                     " result=" + std::to_string(static_cast<int>(evt.result)) +
                     " dt=" + std::to_string(evt.pressTime - evt.time) + "ms");
    };
    m_judgeQueue.onMiss = [this](const gameplay::NoteMissEvent& evt) {
        MM_LOG_DEBUG("Playing", "Miss: col=" + std::to_string(evt.col));
        m_missCount++;                                       // 统计 Miss
        m_comboManager.onMiss();                             // 断连
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        m_popups.push_back({evt.col, gameplay::JudgmentResult::Miss, JudgePopup::DURATION});

        // 偏移条：记录 auto-miss（timing 取 hit50 边界值，超出 50 窗口）
        if (m_offsetBarEnabled) {
            float od = m_beatmap.difficulty.od;
            int32_t hit50W = static_cast<int32_t>(std::max(0.0f, 200.0f - 10.0f * od));
            int64_t missTiming = static_cast<int64_t>(hit50W);
            m_offsetBarMarks.push_back({evt.time + missTiming, evt.time,
                                        missTiming,
                                        OffsetBarMark::DURATION,
                                        gameplay::JudgmentResult::Miss});
        }
    };
    m_judgeQueue.onHoldTail = [this](const gameplay::HoldTailEvent& evt) {
        handleHoldTailEvent(evt);                            // Hold 松手/超时判定
    };

    // ── 初始化阵型控制器（变阵时间轴）──
    m_formationCtrl.load(m_beatmap.formations);

    // ── 初始化列滚动窗口（总列数 > 4 时才需要滚动）──
    int32_t initCols = m_formationCtrl.currentCols();        // 当前阵型总列数
    if (initCols > KEY_COUNT) {                              // 列数超过 4 键
        m_scrollWindow.startCol = 0;                         // 窗口 [0..3]
        m_scrollWindow.endCol = KEY_COUNT - 1;
    } else {
        m_scrollWindow.startCol = 0;
        m_scrollWindow.endCol = initCols - 1;                // 列少时窗口覆盖全部列
    }
    m_scrollWindow.scrolling = false;                        // 初始无滚动动画
    m_lastTransitionEndMs = 0;                               // 上次滚动/变阵结束时刻

    // ── 设置 HP 消耗速率（谱面 HP 难度）──
    m_hpManager.setDrainRate(m_beatmap.difficulty.hp);

    // ── 应用模组（由 SongSelect 传入 mod id 列表）──
    bool noFailEnabled = false;
    m_autoplay = false;
    for (const auto& id : m_modIds) {                        // 遍历启用的 mod
        if (id == "nofail") {
            noFailEnabled = true;
        } else if (id == "autoplay") {
            m_autoplay = true;                               // 自动按键，锁定玩家输入
        }
    }
    if (noFailEnabled) {
        m_hpManager.setMod(std::make_shared<gameplay::NoFailMod>());  // HP 不低于下限
        MM_LOG_INFO("Playing", "NoFail mod enabled");
    } else {
        m_hpManager.setMod(nullptr);                         // 清除 mod
    }
    if (m_autoplay) {
        MM_LOG_INFO("Playing", "Autoplay mod enabled");
    }

    // ── 读取偏移条 / 调试 HUD 配置 ──
    // 正值表示音频听感偏晚：判定时间整体向前修正，视觉/滚动仍使用原始歌曲时间。
    m_timingOffsetMs = platform::Config::getInt(platform::Config::KEY_TIMING_OFFSET, 0);
    m_debugHudEnabled = platform::Config::getInt(platform::Config::KEY_DEBUG_HUD, 0) != 0;
    m_offsetBarEnabled = platform::Config::getInt(platform::Config::KEY_OFFSET_BAR, 0) != 0;
    m_offsetBarMarks.clear();

    // ── 统计 note 总数（结算界面用）──
    m_totalNotes = static_cast<int>(m_beatmap.notes.size());

    // ── 计算第一个 note 的时间（前导倒计时基准）──
    m_firstNoteTimeMs = 0;
    if (!m_beatmap.notes.empty()) {
        m_firstNoteTimeMs = m_beatmap.notes.front().time;   // notes 已按时间排序
    }

    // ── 前导倒计时：第一个 note 前等待，期间只显示倒计时 HUD ──
    m_leadInActive = true;
    m_matrixVisible = false;                                 // 矩阵/note 尚未显示

    // ── 设置 Renderer（背景、阵型、note 数据）──
    auto& renderer = Kernel::instance().renderer();
    renderer.setGameplayRendering(false);                    // 前导阶段不渲染游玩层

    float bgDim = platform::Config::getFloat(platform::Config::KEY_BG_DIM, 0.67f);
    renderer.setBgDim(bgDim);                                // 背景压暗

    if (!m_bgImagePath.empty()) {                            // SongSelect 传入的背景
        renderer.setBackgroundPath(m_bgImagePath);
    }

    if (!m_beatmap.formations.empty()) {                     // 初始阵型
        const auto& f = m_beatmap.formations[0];
        renderer.setFormation(f.rows, f.cols, f.blockSize);
    }

    renderer.setNotes(m_beatmap.notes, m_beatmap.difficulty.ar);  // 提交 note 供 OpenGL 渲染

    // ── 开始播放 BGM ──
    std::string audioPath = m_beatmap.meta.audioFile;        // 相对谱面目录的路径
    if (!audioPath.empty()) {
        auto absPath = std::filesystem::path(m_beatmapFile).parent_path() / audioPath;
        if (std::filesystem::exists(absPath)) {              // 优先相对谱面文件解析
            audioPath = std::filesystem::absolute(absPath).string();
        }
    }
    if (!m_audio.playSong(audioPath)) {                      // 尝试播放
        MM_LOG_WARN("Playing", "Failed to play: " + audioPath);
        auto resolved = platform::FileSystem::safeResolve("assets", m_beatmap.meta.audioFile);
        if (resolved.ok()) {                                 // fallback 到 assets 根
            if (!m_audio.playSong(resolved.value())) {
                MM_LOG_WARN("Playing", "Audio playback failed - continuing without audio");
            }
        }
    }

    // C2：优先 WASAPI 设备钟；失败回退 miniaudio cursor
    m_playhead = audio::tryMakeWasapiPlayhead(m_audio);
    if (!m_playhead) {
        m_playhead = audio::makeMiniaudioCursorPlayhead(m_audio);
        MM_LOG_INFO("Playing", "Playhead backend: miniaudio-cursor (WASAPI unavailable)");
    } else {
        MM_LOG_INFO("Playing", "Playhead backend: wasapi");
    }
    m_playhead->noteTimelineEpoch(m_audio.positionMs());
    m_lastPlayheadSampleHostMs = 0;  // 立刻打一条首采样
    m_playheadWindowStats = {};
    m_cursorSmooth = {};
    {
        const int64_t ph = playheadPositionMs();
        Kernel::instance().clock().syncFromAudio(ph);
        samplePlayheadLog(Kernel::instance().clock().interpolatedNowMs(), ph);
    }

    m_gameplayInitialized = true;                            // 标记完成，update 开始主循环逻辑
    m_needsReinit = false;
    MM_LOG_INFO("Playing", "Gameplay initialized - " + std::to_string(m_totalNotes) +
                " notes, first note at " + std::to_string(m_firstNoteTimeMs) + "ms");
}

/// 从 AudioPlayhead 同步游戏时钟（主循环处理输入前调用）
void PlayingState::syncClockFromAudio() {
    if (!m_gameplayInitialized || !m_playhead) return;
    const int64_t ph = m_playhead->positionMs();
    auto& clock = Kernel::instance().clock();
    // playhead 尚未有效时钉在 0，避免 HostClock 在 ph=0 停滞期间自由跑出 +100ms 尖峰
    if (ph <= 0) {
        clock.reset();
        return;
    }
    clock.syncFromAudio(ph);
}

int64_t PlayingState::playheadPositionMs() const {
    return m_playhead->positionMs();
}

int64_t PlayingState::smoothedCursorMs() {
    const int64_t raw = m_audio.positionMs();
    const int64_t host = time::HostClock::nowMs();
    if (!m_cursorSmooth.has || raw != m_cursorSmooth.lastRawMs) {
        m_cursorSmooth.lastRawMs = raw;
        m_cursorSmooth.hostAtRawMs = host;
        m_cursorSmooth.has = true;
        return raw;
    }
    // write cursor 约 10ms 一跳；停滞期间按墙钟外推，ph-cu 才反映稳态偏差而非台阶
    int64_t smooth = m_cursorSmooth.lastRawMs + (host - m_cursorSmooth.hostAtRawMs);
    constexpr int64_t kMaxFreeRunMs = 12;  // 略大于共享 period，防止空跑太远
    if (smooth > m_cursorSmooth.lastRawMs + kMaxFreeRunMs) {
        smooth = m_cursorSmooth.lastRawMs + kMaxFreeRunMs;
    }
    if (smooth < raw) {
        smooth = raw;
    }
    return smooth;
}

void PlayingState::samplePlayheadLog(int64_t songNowMs, int64_t playheadMs) {
    // 同快照残差：调用方必须先 sync(playhead) 再读 song，且勿在中间重读 playhead
    const int64_t songPh = songNowMs - playheadMs;
    auto& w = m_playheadWindowStats;
    if (w.samples == 0) {
        w.songPhMin = songPh;
        w.songPhMax = songPh;
        w.absMax = songPh < 0 ? -songPh : songPh;
    } else {
        if (songPh < w.songPhMin) w.songPhMin = songPh;
        if (songPh > w.songPhMax) w.songPhMax = songPh;
        const int64_t absPh = songPh < 0 ? -songPh : songPh;
        if (absPh > w.absMax) w.absMax = absPh;
    }
    if (songPh > 1 || songPh < -1) {
        ++w.outliers;
    }
    if (w.hasLastPlayhead) {
        int64_t step = playheadMs - w.lastPlayheadMs;
        if (step < 0) step = -step;
        if (step > w.phStepMax) w.phStepMax = step;
    }
    w.lastPlayheadMs = playheadMs;
    w.hasLastPlayhead = true;
    ++w.samples;

    const int64_t hostNow = time::HostClock::nowMs();
    if (m_lastPlayheadSampleHostMs != 0
        && hostNow - m_lastPlayheadSampleHostMs < PLAYHEAD_SAMPLE_INTERVAL_MS) {
        return;
    }
    m_lastPlayheadSampleHostMs = hostNow;

    const int64_t cursorRaw = m_audio.positionMs();
    const int64_t cursorSmooth = smoothedCursorMs();
    const char* backend = m_playhead ? m_playhead->backendName() : "?";
    // ph-cu：对平滑 cursor，避免 write-cursor 10ms 台阶造成假锯齿
    // ph-cuRaw：原始对比，仅备查
    MM_LOG_INFO("PlayheadSample",
                "backend=%s song=%lld playhead=%lld cursor=%lld cursorSm=%lld "
                "ph-cu=%+lld ph-cuRaw=%+lld song-ph=%+lld "
                "win[n=%lld min=%+lld max=%+lld absMax=%lld outliers=%lld phStepMax=%lld] host=%lld",
                backend,
                static_cast<long long>(songNowMs),
                static_cast<long long>(playheadMs),
                static_cast<long long>(cursorRaw),
                static_cast<long long>(cursorSmooth),
                static_cast<long long>(playheadMs - cursorSmooth),
                static_cast<long long>(playheadMs - cursorRaw),
                static_cast<long long>(songPh),
                static_cast<long long>(w.samples),
                static_cast<long long>(w.songPhMin),
                static_cast<long long>(w.songPhMax),
                static_cast<long long>(w.absMax),
                static_cast<long long>(w.outliers),
                static_cast<long long>(w.phStepMax),
                static_cast<long long>(hostNow));

    // 新窗口
    w = {};
}

/// 每帧更新：时钟、前导、判定、滚动、变阵、HP、结束检测（Playing 主循环）
GameState PlayingState::update(float dt) {
    if (!m_gameplayInitialized) return GameState::Count;       // 未初始化则保持 Playing 状态

    // ── 从音频 Playhead 同步歌曲时钟（同快照读 song，避免假 song-ph=-1）──
    auto& kernel = Kernel::instance();
    const int64_t playheadMs = playheadPositionMs();
    if (playheadMs <= 0) {
        kernel.clock().reset();
    } else {
        kernel.clock().syncFromAudio(playheadMs);
    }
    int64_t nowMs = kernel.clock().interpolatedNowMs();
    samplePlayheadLog(nowMs, playheadMs);
    const int64_t judgeNowMs = toJudgeSongTimeMs(nowMs);     // 减去用户 timing offset 的判定时间
    float od = m_beatmap.difficulty.od;                      // 总体难度，影响判定窗口

    // ── Skip 功能：空格键跳过前导等待 ──
    if (m_leadInActive) {
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);
        SDL_Scancode spaceScan = SDL_GetScancodeFromKey(SDLK_SPACE);
        if (keyState[spaceScan]) {                           // 空格按住
            int64_t skipTarget = m_firstNoteTimeMs - SKIP_TARGET_BEFORE_MS;  // 跳到首 note 前 3s
            if (skipTarget > 0 && skipTarget > nowMs) {      // 目标在未来才 seek
                m_audio.seekTo(skipTarget);                  // 音频跳转
                m_playhead->noteTimelineEpoch(skipTarget);   // WASAPI 歌曲时间线重锚
                kernel.clock().syncFromAudio(playheadPositionMs());
                nowMs = skipTarget;
                MM_LOG_INFO("Playing", "Skipped to " + std::to_string(skipTarget) + "ms");
            }
        }
    }

    // ── 前导等待：直到接近第一个 note 才开始正式游玩 ──
    if (m_leadInActive) {
        int64_t gameplayStartMs = m_firstNoteTimeMs - LEAD_IN_BEFORE_NOTE_MS;  // 首 note 前 3s
        if (nowMs >= gameplayStartMs) {                      // 前导结束
            m_leadInActive = false;
            MM_LOG_INFO("Playing", "Lead-in complete, nowMs=" + std::to_string(nowMs) +
                        " audioPos=" + std::to_string(playheadPositionMs()) +
                        " firstNote=" + std::to_string(m_firstNoteTimeMs) + "ms");
        }
        kernel.renderer().setGameplayRendering(false);       // 前导期间不画矩阵
        return GameState::Count;                             // 提前返回，跳过判定等逻辑
    }

    // ── 矩阵可见性：approach 窗口内才显示 note 矩阵 ──
    if (!m_matrixVisible) {
        float approachMs = 1800.0f - m_beatmap.difficulty.ar * 120.0f;  // AR 越高 approach 越短
        if (approachMs < 300.0f) approachMs = 300.0f;        // 下限 300ms
        if (nowMs >= m_firstNoteTimeMs - static_cast<int64_t>(approachMs)) {
            m_matrixVisible = true;                          // 允许渲染 gameplay 层
        }
    }

    // ── 判定与 Autoplay：滚动/变阵期间可能锁定 ──
    bool inTransition = m_formationCtrl.inTransition(nowMs); // 变阵动画进行中
    bool inScroll = m_scrollWindow.scrolling;                // 列滚动动画进行中
    bool judgmentBlocked = inScroll || isFormationJudgmentBlocked(nowMs);  // 是否禁止判定
    if (!judgmentBlocked) {                                  // 允许判定时
        if (m_autoplay) {
            for (int k = 0; k < KEY_COUNT; ++k) {          // 衰减 Tap 高亮计时
                if (m_autoKeyFlash[k] > 0.0f) {
                    m_autoKeyFlash[k] -= dt;
                }
            }
            processAutoplay(nowMs, od);                      // 模拟 D/F/J/K 按键
        }
        m_judgeQueue.update(judgeNowMs, od);                 // auto-miss、Hold 超时等
    }

    // ── 列滚动逻辑 ──
    if (inScroll && m_scrollWindow.finished(nowMs)) {        // 滚动动画时间到
        completeScroll();                                    // 提交新窗口边界
    }
    if (!inScroll && !inTransition) {                        // 无滚动且无变阵时才检测触发
        checkAndTriggerScroll(nowMs);                        // 可能需要启动新滚动
    }
    // 同步滚动状态到渲染器（progress 驱动像素偏移，三处 gw 一致）
    kernel.renderer().setScrollState(m_scrollWindow.startCol, m_scrollWindow.endCol,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetStartCol : m_scrollWindow.startCol,
                                      m_scrollWindow.scrolling ? m_scrollWindow.targetEndCol : m_scrollWindow.endCol,
                                      m_scrollWindow.scrolling, m_scrollWindow.progress(nowMs));
    {
        std::array<size_t, 8> heads = {};                    // 各列队列头索引（渲染消 note 用）
        for (int32_t c = 0; c < m_judgeQueue.columnCount() && c < 8; ++c) {
            heads[c] = m_judgeQueue.columnQueue(c).head;
        }
        kernel.renderer().setColumnHeads(heads, m_judgeQueue.columnCount());
        kernel.renderer().setHitEffects(m_hitEffects);     // 格子击打闪光
    }

    // ── 休息段检测：>10s 空挡渐变隐藏游戏界面 ──
    if (!m_beatmap.notes.empty()) {
        int64_t nextNoteTime = INT64_MAX;                    // 当前时间之后首个 note
        int64_t prevNoteTime = 0;                            // 当前时间之前最后 note
        for (const auto& n : m_beatmap.notes) {
            if (n.time > nowMs) { nextNoteTime = n.time; break; }
            prevNoteTime = n.time;
        }

        float gameplayFade = 1.0f;                           // 1=全显，0=全隐
        if (nextNoteTime != INT64_MAX && prevNoteTime > 0) {
            int64_t gap = nextNoteTime - prevNoteTime;       // 两 note 间隔
            if (gap > 10000) {                               // 超过 10s 视为休息段
                int64_t fadeOutStart = prevNoteTime + 1000;  // 末 note 后 1s 开始淡出
                int64_t fadeInStart = nextNoteTime - 3000;   // 下 note 前 3s 开始淡入
                int64_t fadeInEnd = nextNoteTime - 2000;
                if (nowMs < fadeOutStart) {
                    gameplayFade = 1.0f;                     // 尚未淡出
                } else if (nowMs < fadeOutStart + 1000) {
                    gameplayFade = 1.0f - static_cast<float>(nowMs - fadeOutStart) / 1000.0f;
                } else if (nowMs < fadeInStart) {
                    gameplayFade = 0.0f;                     // 完全隐藏
                } else if (nowMs < fadeInEnd) {
                    gameplayFade = static_cast<float>(nowMs - fadeInStart) / 1000.0f;
                } else {
                    gameplayFade = 1.0f;                     // 淡入完成
                }
            }
        }
        kernel.renderer().setGameplayFade(gameplayFade);
    }

    // ── 变阵更新：到达 formation 时间点切换行列/blockSize ──
    bool formationChanged = m_formationCtrl.update(nowMs);
    if (formationChanged) {
        const auto& prev = m_formationCtrl.formationAt(
            m_formationCtrl.currentIndex() > 0 ? m_formationCtrl.currentIndex() - 1 : 0);
        const auto& next = m_formationCtrl.current();
        int64_t durationMs = next.transformDurationMs;
        if (durationMs > 0) {                              // 有过渡动画
            m_formationCtrl.setTransitionDuration(durationMs);
            kernel.renderer().beginFormationTransition(prev.rows, prev.cols, prev.blockSize,
                                                       next.rows, next.cols, next.blockSize,
                                                       next.transformType);
            m_lastTransitionEndMs = std::max(m_lastTransitionEndMs, next.time + durationMs);
        } else {                                             // 瞬间切换
            kernel.renderer().setFormation(next.rows, next.cols, next.blockSize);
            m_lastTransitionEndMs = std::max(m_lastTransitionEndMs, next.time);
        }

        snapScrollWindowForFormation(next.cols, next.time);  // 变阵后对齐滚动窗
        m_scrollWindow.targetStartCol = m_scrollWindow.startCol;
        m_scrollWindow.targetEndCol = m_scrollWindow.endCol;
        m_scrollWindow.scrolling = false;                    // 变阵时取消进行中的滚动
    }

    // ── 变阵过渡进度（0~1 驱动 renderer 插值）──
    if (inTransition) {
        float progress = m_formationCtrl.transitionProgress(nowMs);
        kernel.renderer().updateFormationTransition(progress);
    }

    // ── HP 被动流失（与谱面 HP 难度相关）──
    m_hpManager.drainPerFrame(dt);

    // ── 判定弹出计时衰减 ──
    for (auto& p : m_popups) {
        p.timer -= dt;
    }
    m_popups.erase(
        std::remove_if(m_popups.begin(), m_popups.end(),
                       [](const JudgePopup& p) { return p.timer <= 0.0f; }),
        m_popups.end());

    for (auto& hit : m_hitEffects) {                         // 格子击中 alpha 衰减
        hit.alpha -= dt / HIT_EFFECT_DURATION;
    }
    m_hitEffects.erase(
        std::remove_if(m_hitEffects.begin(), m_hitEffects.end(),
                       [](const renderer::CellHitEffect& h) { return h.alpha <= 0.0f; }),
        m_hitEffects.end());

    // ── 偏移条标记衰减 ──
    if (m_offsetBarEnabled) {
        for (auto& m : m_offsetBarMarks) {
            m.timer -= dt;
        }
        m_offsetBarMarks.erase(
            std::remove_if(m_offsetBarMarks.begin(), m_offsetBarMarks.end(),
                           [](const OffsetBarMark& m) { return m.timer <= 0.0f; }),
            m_offsetBarMarks.end());
    }

    // ── 歌曲结束检测 ──
    if (!m_audio.isPlaying()) {                              // BGM 已停止
        if (playheadPositionMs() > 0 || m_judgeQueue.finished()) {  // 曾播放过或 note 判完
            m_songFinished = true;
        }
    }

    // ── HP 死亡检测 ──
    if (m_hpManager.isDead()) {
        m_playerDied = true;
    }

    // ── 状态过渡：进入结算 ──
    if (m_songFinished || m_playerDied) {
        auto* result = kernel.stateManager().getStateAs<ResultState>(GameState::Result);
        if (result) {                                        // 写入结算数据
            result->score = static_cast<int>(m_scoreManager.totalScore());
            result->maxCombo = m_comboManager.max();
            result->hit300Count = m_hit300Count;
            result->hit100Count = m_hit100Count;
            result->hit50Count = m_hit50Count;
            result->missCount = m_missCount;
            result->totalNotes = m_totalNotes;
            result->playerDied = m_playerDied;
            result->songTitle = m_beatmap.meta.title;
        }
        return GameState::Result;                            // 请求切换到 Result
    }

    kernel.renderer().setGameplayRendering(m_matrixVisible); // 控制本帧是否画 note 矩阵

    return GameState::Count;                                   // 保持 Playing
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

/// 检测 approach 窗口内最早 note 是否超出当前窗口，触发列滚动（多列谱面 D/F/J/K 窗口平移）
void PlayingState::checkAndTriggerScroll(int64_t nowMs) {
    int32_t totalCols = m_formationCtrl.currentCols();         // 当前阵型总列数
    if (totalCols <= KEY_COUNT) return;                      // ≤4 列无需滚动

    // 有活跃 Hold 时硬阻止滚动，和 parser 的 Hold 阻塞/降级策略保持一致。
    for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
        if (m_judgeQueue.getActiveHold(col)) {               // 任一带 Hold 则本帧不滚
            return;
        }
    }

    const int64_t approachMs = beatmap::approachMs(m_beatmap.difficulty.ar);  // AR 推导 approach 时长

    int32_t windowStart = m_scrollWindow.startCol;           // 当前活跃窗口
    int32_t windowEnd = m_scrollWindow.endCol;

    // ── 只跟踪 approach 窗口内最早需要判定的 note 所在列 ──
    int64_t earliestNoteTime = INT64_MAX;                    // 最早待判 note 时间
    int32_t earliestNoteCol = -1;                            // 对应列号，-1 表示无

    for (int32_t col = 0; col < m_judgeQueue.columnCount(); ++col) {
        const auto& colQ = m_judgeQueue.columnQueue(col);
        if (colQ.finished()) continue;                       // 该列已判完

        const auto& note = colQ.front();                     // 队首为下一个待判 note
        const float timeDiff = static_cast<float>(note.time - nowMs);  // 距 note 时刻剩余 ms

        // 仅对未过期且在 approach 窗口内的 note 参与滚动决策
        if (timeDiff <= static_cast<float>(approachMs) && timeDiff > 0) {
            if (note.time < earliestNoteTime) {                // 取时间最早者
                earliestNoteTime = note.time;
                earliestNoteCol = col;
            }
        }
    }

    // 最早 note 不在当前 4 列窗口内 → 计算目标窗口并可能启动滚动动画
    if (earliestNoteCol >= 0 &&
        (earliestNoteCol < windowStart || earliestNoteCol > windowEnd)) {
        const float od = m_beatmap.difficulty.od;
        const float ar = m_beatmap.difficulty.ar;
        const int64_t scrollTrigger =
            beatmap::scrollTriggerMs(earliestNoteTime, m_lastTransitionEndMs, ar);  // 最早可触发时刻
        const int64_t blockedStart = beatmap::scrollStartMsFromWindowHolds(
            windowStart, windowEnd, earliestNoteTime, scrollTrigger, od, m_beatmap.notes);  // Hold 阻塞推迟
        if (nowMs < blockedStart) {                          // 尚未到允许滚动时刻
            return;
        }
        const int64_t scrollStartMs = std::max(nowMs, blockedStart);

        const int32_t targetStart = beatmap::chooseScrollWindowStart(
            windowStart, earliestNoteCol, totalCols, KEY_COUNT,
            earliestNoteTime, m_beatmap.notes);

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

/// 滚动动画完成：将目标窗口提交为当前窗口，更新 lastTransitionEndMs
void PlayingState::completeScroll() {
    int32_t newStart = m_scrollWindow.targetStartCol;
    int32_t newEnd = m_scrollWindow.targetEndCol;

    m_scrollWindow.startCol = newStart;                      // 不移动 note 数据，只改 D/F/J/K 映射列
    m_scrollWindow.endCol = newEnd;
    m_scrollWindow.scrolling = false;                        // 解除滚动锁判定
    m_lastTransitionEndMs = std::max(
        m_lastTransitionEndMs,
        m_scrollWindow.scrollStartMs + static_cast<int64_t>(m_scrollWindow.scrollDurationMs));

    MM_LOG_INFO("Playing", "Scroll completed: window now " +
                std::to_string(m_scrollWindow.startCol) + "-" +
                std::to_string(m_scrollWindow.endCol));
}

/// 处理单条键盘事件（SDL KEYDOWN/UP → 列映射 → JudgeQueue，由 Kernel 在同步时钟后调用）
void PlayingState::handleKeyEvent(int32_t key, bool pressed, int64_t eventTimeMs) {
    if (!m_gameplayInitialized || m_leadInActive) return;    // 未就绪或前导阶段忽略
    if (m_autoplay) return;                                  // Autoplay 锁定玩家输入

    const int64_t judgeEventTimeMs = toJudgeSongTimeMs(eventTimeMs);  // 应用 timing offset

    // 更新按键显示状态（即使暂时不判定，避免 HUD 与物理按键脱节）
    int keyIndex = -1;
    for (int k = 0; k < KEY_COUNT; ++k) {                    // 匹配 D/F/J/K
        if (key == KEY_CODES[k]) {
            keyIndex = k;
            m_curKeyDown[k] = pressed;                       // 供 ImGui 按键高亮
            break;
        }
    }
    if (keyIndex < 0) return;                                // 非游玩键忽略

    auto mapping = getKeyMapping();                          // 当前滚动窗口下的列映射
    int32_t column = -1;
    if (isFormationJudgmentBlocked(eventTimeMs) || m_scrollWindow.scrolling) {
        return;                                              // 滚动/变阵锁判定时不送 JudgeQueue
    }
    for (const auto& m : mapping) {
        if (m.sdlKey == key) {
            column = m.column;                               // SDL 键 → 谱面列
            break;
        }
    }
    if (column < 0) return;                                  // 映射失败

    const float od = m_beatmap.difficulty.od;
    if (pressed) {                                           // 按键按下 → Tap/Hold 头判定
        int64_t noteTime = 0;
        int32_t noteRow = 0;
        bool isTapNote = true;
        if (column >= 0 && column < m_judgeQueue.columnCount()) {
            const auto& colQ = m_judgeQueue.columnQueue(column);
            if (!colQ.finished()) {                          // 取队首 note 信息（特效/偏移条用）
                noteTime = colQ.front().time;
                noteRow = colQ.front().row;
                isTapNote = !colQ.front().isHold();
            }
        }
        auto result = m_judgeQueue.onKeyPress(judgeEventTimeMs, column, od);
        MM_LOG_DEBUG("Playing", "KeyDown: col=" + std::to_string(column) +
                     " eventTimeMs=" + std::to_string(eventTimeMs) +
                     " judgeTimeMs=" + std::to_string(judgeEventTimeMs) +
                     " result=" + std::to_string(static_cast<int>(result)));
        handlePressResult(result, column, noteRow, judgeEventTimeMs, noteTime, isTapNote);
    } else {                                                 // 按键释放 → Hold 尾判定
        m_judgeQueue.onKeyRelease(judgeEventTimeMs, column, od);
        MM_LOG_DEBUG("Playing", "KeyUp: col=" + std::to_string(column) +
                     " eventTimeMs=" + std::to_string(eventTimeMs) +
                     " judgeTimeMs=" + std::to_string(judgeEventTimeMs));
    }
}

/// 处理 Tap/Hold 头部按键判定结果，更新分数/连击/HP/特效/偏移条
void PlayingState::handlePressResult(gameplay::JudgmentResult result, int32_t column, int32_t row,
                                     int64_t pressTime, int64_t noteTime, bool isTapNote) {
    switch (result) {
    case gameplay::JudgmentResult::Hit300:
        m_hit300Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit300, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit300);
        m_audio.playSfx(audio::SfxType::HitNormal);
        break;
    case gameplay::JudgmentResult::Hit100:
        m_hit100Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit100, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit100);
        m_audio.playSfx(audio::SfxType::HitNormal);
        break;
    case gameplay::JudgmentResult::Hit50:
        m_hit50Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit50, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit50);
        m_audio.playSfx(audio::SfxType::HitNormal);
        break;
    case gameplay::JudgmentResult::Miss:
        m_missCount++;
        m_comboManager.onMiss();
        m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
        m_scoreManager.addScore(gameplay::JudgmentResult::Miss, 0);
        break;
    case gameplay::JudgmentResult::Ignored:                  // 空按、窗口外等
        return;                                              // 不更新统计与 popup
    }

    m_lastHitDebug.judgeMs = pressTime;                      // Debug HUD 用
    m_lastHitDebug.noteMs = noteTime;
    m_lastHitDebug.timing = pressTime - noteTime;
    m_lastHitDebug.result = result;

    if (isTapNote &&                                          // Hold 头不播 cell 闪光（Hold 持续按住）
        (result == gameplay::JudgmentResult::Hit300 ||
         result == gameplay::JudgmentResult::Hit100 ||
         result == gameplay::JudgmentResult::Hit50)) {
        m_hitEffects.push_back({ column, row, 1.0f });
    }

    if (m_offsetBarEnabled && result != gameplay::JudgmentResult::Ignored) {
        OffsetBarMark mark;
        mark.hitTime = pressTime;
        mark.noteTime = noteTime;
        mark.timing = pressTime - noteTime;                  // 早负晚正
        mark.timer = OffsetBarMark::DURATION;
        mark.result = result;
        m_offsetBarMarks.push_back(mark);
    }

    m_popups.push_back({column, result, JudgePopup::DURATION});  // 300/100/50/MISS 浮字
}

/// 处理 Hold 尾部释放判定结果
void PlayingState::handleHoldTailEvent(const gameplay::HoldTailEvent& evt) {
    const int32_t column = evt.col;
    switch (evt.result) {
    case gameplay::HoldReleaseResult::Hit300:
        m_hit300Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit300, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit300);
        m_popups.push_back({column, gameplay::JudgmentResult::Hit300, JudgePopup::DURATION});
        break;
    case gameplay::HoldReleaseResult::Hit100:
        m_hit100Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit100, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit100);
        m_popups.push_back({column, gameplay::JudgmentResult::Hit100, JudgePopup::DURATION});
        break;
    case gameplay::HoldReleaseResult::Hit50:
        m_hit50Count++;
        m_hitNotes++;
        m_comboManager.onHit();
        m_scoreManager.addScore(gameplay::JudgmentResult::Hit50, m_comboManager.current());
        m_hpManager.onJudgment(gameplay::JudgmentResult::Hit50);
        m_popups.push_back({column, gameplay::JudgmentResult::Hit50, JudgePopup::DURATION});
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
        mark.result = evt.result == gameplay::HoldReleaseResult::Hit300
                          ? gameplay::JudgmentResult::Hit300
                      : evt.result == gameplay::HoldReleaseResult::Hit100
                          ? gameplay::JudgmentResult::Hit100
                      : evt.result == gameplay::HoldReleaseResult::Hit50
                          ? gameplay::JudgmentResult::Hit50
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

    const float uiScale = ImGui::GetIO().DisplaySize.y > 0.0f
        ? ImGui::GetIO().DisplaySize.y / 1080.0f : 1.0f;
    const float pad = 12.0f * uiScale;

    // HUD 浮层：禁止滚动条；文字类用 AlwaysAutoResize，避免固定高度装不下时出现滚动条边
    const ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBackground |
                                      ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings;
    const ImGuiWindowFlags fixedFlags = ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0, 0, 0, 0));

    auto popHudChrome = []() {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    };

    // ── 前导倒计时 ──
    if (m_leadInActive) {
        auto& kernel = Kernel::instance();
        int64_t nowMs = kernel.clock().interpolatedNowMs();
        int64_t gameplayStartMs = m_firstNoteTimeMs - LEAD_IN_BEFORE_NOTE_MS;
        int64_t remainingMs = gameplayStartMs - nowMs;

        if (remainingMs > 0) {
            int countdown = static_cast<int>(remainingMs / 1000) + 1;
            if (countdown < 1) countdown = 1;

            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 60.0f * uiScale,
                                            ImGui::GetIO().DisplaySize.y * 0.5f - 40.0f * uiScale));
            ImGui::Begin("##LeadIn", nullptr, hudFlags);
            ImGui::SetWindowFontScale(3.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
            ImGui::Text("%d", countdown);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);
            ImGui::End();

            // Skip 提示（仅当第一个note距离歌曲开始超过5秒时显示）
            if (m_firstNoteTimeMs > 5000) {
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 120.0f * uiScale,
                                                ImGui::GetIO().DisplaySize.y * 0.5f + 50.0f * uiScale));
                ImGui::Begin("##SkipHint", nullptr, hudFlags);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 0.7f));
                ImGui::Text("SPACE to skip");
                ImGui::PopStyleColor();
                ImGui::End();
            }
        }
        popHudChrome();
        return;
    }

    const float fontBase = ImGui::GetFontSize() > 0.1f ? ImGui::GetFontSize() : Theme::FONT_SIZE;
    // 目标视觉字号 / 当前 atlas 字号 → WindowFontScale，使分辨率变化时观感一致
    const float debugFontScale = (1.35f * Theme::FONT_SIZE * uiScale) / fontBase;
    const float scoreFontScale = (2.0f * Theme::FONT_SIZE * uiScale) / fontBase;
    const float comboFontScale = (1.5f * Theme::FONT_SIZE * uiScale) / fontBase;

    // ── Debug HUD（Settings 可开关）──
    if (m_debugHudEnabled) {
        auto& kernel = Kernel::instance();
        const int64_t songNowMs = kernel.clock().interpolatedNowMs();
        const int64_t visualLeadMs = platform::Config::getInt(platform::Config::KEY_VISUAL_LEAD, 16);
        const int64_t playheadMs = playheadPositionMs();
        const int64_t cursorMs = m_audio.positionMs();
        const int64_t cursorSm = smoothedCursorMs();
        const char* playheadBackend = m_playhead ? m_playhead->backendName() : "?";

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
        case gameplay::JudgmentResult::Hit300: resultLabel = "300"; break;
        case gameplay::JudgmentResult::Hit100: resultLabel = "100"; break;
        case gameplay::JudgmentResult::Hit50:  resultLabel = "50"; break;
        case gameplay::JudgmentResult::Miss:   resultLabel = "Miss"; break;
        default: break;
        }

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 340.0f * uiScale, 12.0f * uiScale));
        ImGui::Begin("##DebugHUD", nullptr, hudFlags);
        ImGui::SetWindowFontScale(debugFontScale);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Text("SONG %lld ms | PLAYHEAD %lld (%s) | CURSOR %lld (sm %lld) | PH-CU %+lld",
                    songNowMs, playheadMs, playheadBackend, cursorMs, cursorSm,
                    playheadMs - cursorSm);
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
        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

    // ── Top-left: Score（贴左上角，字号随分辨率）──
    ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
    ImGui::Begin("##ScoreHUD", nullptr, hudFlags);
    ImGui::SetWindowFontScale(scoreFontScale);
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::Text("%08d", static_cast<int>(m_scoreManager.totalScore()));
    ImGui::PopStyleColor();

    if (m_comboManager.current() > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
        ImGui::SetWindowFontScale(comboFontScale);
        ImGui::Text("%dx COMBO", m_comboManager.current());
        ImGui::PopStyleColor();
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();

    // ── Top-right: Judgment counts ──
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - pad, pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::Begin("##JudgeHUD", nullptr, hudFlags);
    ImGui::SetWindowFontScale(std::max(0.85f, uiScale * (Theme::FONT_SIZE / ImGui::GetFontSize())));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::Text("300:  %d", m_hit300Count);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.702f, 1.0f, 0.0f, 1.0f));
    ImGui::Text("100:  %d", m_hit100Count);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
    ImGui::Text("50:   %d", m_hit50Count);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    ImGui::Text("Miss: %d", m_missCount);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();

    // ── Bottom: HP bar ──
    const float hpMargin = 20.0f * uiScale;
    const float hpBarH = 20.0f * uiScale;
    const float hpWinH = 30.0f * uiScale;
    float barWidth = ImGui::GetIO().DisplaySize.x - 2.0f * hpMargin;
    ImGui::SetNextWindowPos(ImVec2(hpMargin, ImGui::GetIO().DisplaySize.y - 50.0f * uiScale));
    ImGui::SetNextWindowSize(ImVec2(barWidth, hpWinH));

    ImGui::Begin("##HPHUD", nullptr, fixedFlags);
    float hp = m_hpManager.hp();
    ImVec4 hpColor = hp > 0.5f ?
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.8f) :
        hp > 0.25f ? ImVec4(0.94f, 0.62f, 0.15f, 0.8f) :
                     ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.8f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
    ImGui::ProgressBar(hp, ImVec2(barWidth - hpMargin, hpBarH), "");
    ImGui::PopStyleColor();
    ImGui::End();

    // ── Offset Bar（偏移条）──
    if (m_offsetBarEnabled) {
        // Stable 窗口：300 / 100 / 50（与 StableJudgeStrategy 一致）
        float od = m_beatmap.difficulty.od;
        int32_t hit300W = static_cast<int32_t>(std::max(0.0f, 80.0f - 6.0f * od));
        int32_t hit100W = static_cast<int32_t>(std::max(0.0f, 140.0f - 8.0f * od));
        int32_t hit50W  = static_cast<int32_t>(std::max(0.0f, 200.0f - 10.0f * od));
        if (hit300W < 1) hit300W = 1;
        if (hit100W <= hit300W) hit100W = hit300W + 1;
        if (hit50W <= hit100W) hit50W = hit100W + 1;

        const float obW = 400.0f * uiScale;
        const float obH = 20.0f * uiScale;
        const float scaleY = ImGui::GetIO().DisplaySize.y / renderer::GridLayout::kScreenH;
        const float matrixBottom = ImGui::GetIO().DisplaySize.y
            - renderer::GridLayout::kMargin * scaleY
            + renderer::GridLayout::kMatrixShiftY * scaleY;
        float obX = (ImGui::GetIO().DisplaySize.x - obW) * 0.5f;
        const float keyH = 44.0f * uiScale;
        const float keyHintY = std::min(matrixBottom + 8.0f * uiScale,
                                        ImGui::GetIO().DisplaySize.y - 58.0f * uiScale - keyH);
        float obY = keyHintY - obH - 10.0f * uiScale;
        float centerX = obX + obW * 0.5f;
        float half50 = obW * 0.5f;
        float half100 = (static_cast<float>(hit100W) / static_cast<float>(hit50W)) * half50;
        float half300 = (static_cast<float>(hit300W) / static_cast<float>(hit50W)) * half50;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // 背景灰色（最外侧，超出 50 窗口）
        dl->AddRectFilled(ImVec2(obX, obY), ImVec2(obX + obW, obY + obH),
                          IM_COL32(60, 60, 70, 200));

        // 50 区域（黄）
        ImU32 hit50Color = IM_COL32(255, 200, 40, 200);
        dl->AddRectFilled(ImVec2(obX, obY),
                          ImVec2(centerX - half100, obY + obH), hit50Color);
        dl->AddRectFilled(ImVec2(centerX + half100, obY),
                          ImVec2(obX + obW, obY + obH), hit50Color);

        // 100 区域（绿）
        ImU32 hit100Color = IM_COL32(100, 200, 50, 200);
        dl->AddRectFilled(ImVec2(centerX - half100, obY),
                          ImVec2(centerX - half300, obY + obH), hit100Color);
        dl->AddRectFilled(ImVec2(centerX + half300, obY),
                          ImVec2(centerX + half100, obY + obH), hit100Color);

        // 300 区域（青）
        dl->AddRectFilled(ImVec2(centerX - half300, obY),
                          ImVec2(centerX + half300, obY + obH),
                          IM_COL32(0, 255, 245, 200));

        // 中心线（0ms 位置）白色
        dl->AddLine(ImVec2(centerX, obY - 2.0f * uiScale),
                    ImVec2(centerX, obY + obH + 2.0f * uiScale),
                    IM_COL32(255, 255, 255, 255), 2.0f * uiScale);

        // 边框
        dl->AddRect(ImVec2(obX, obY), ImVec2(obX + obW, obY + obH),
                    IM_COL32(200, 200, 220, 220), 0.0f, 0, 1.0f * uiScale);

        // 标记点（每次击中 note 时记录的 timing）
        for (const auto& mark : m_offsetBarMarks) {
            // 将 timing 限制在 ±hit50W 范围内用于显示
            int64_t clamped = mark.timing;
            if (clamped < -static_cast<int64_t>(hit50W)) clamped = -hit50W;
            if (clamped >  static_cast<int64_t>(hit50W)) clamped =  hit50W;
            float markX = centerX + (static_cast<float>(clamped) / static_cast<float>(hit50W)) * half50;
            float alpha = std::min(1.0f, mark.timer / OffsetBarMark::DURATION);

            ImU32 markColor;
            int alphaByte = static_cast<int>(255.0f * alpha);
            switch (mark.result) {
            case gameplay::JudgmentResult::Hit300:
                markColor = IM_COL32(0, 255, 245, alphaByte);
                break;
            case gameplay::JudgmentResult::Hit100:
                markColor = IM_COL32(100, 200, 50, alphaByte);
                break;
            case gameplay::JudgmentResult::Hit50:
                markColor = IM_COL32(255, 200, 40, alphaByte);
                break;
            case gameplay::JudgmentResult::Miss:
                markColor = IM_COL32(255, 0, 110, alphaByte);
                break;
            default:
                markColor = IM_COL32(200, 200, 200, alphaByte);
                break;
            }

            // 标记为竖线
            dl->AddLine(ImVec2(markX, obY - 4.0f * uiScale),
                        ImVec2(markX, obY + obH + 4.0f * uiScale),
                        markColor, 3.0f * uiScale);
        }
    }

    // ── Judgment popups ──
    int popupIdx = 0;
    for (const auto& popup : m_popups) {
        float alpha = std::min(1.0f, popup.timer / (JudgePopup::DURATION * 0.3f));
        float offsetY = (1.0f - popup.timer / JudgePopup::DURATION) * 40.0f * uiScale;

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

        ImGui::SetNextWindowPos(ImVec2(colX, ImGui::GetIO().DisplaySize.y / 2 - 80.0f * uiScale - offsetY),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        char windowId[32];
        snprintf(windowId, sizeof(windowId), "##JudgePopup%d", popupIdx++);
        ImGui::Begin(windowId, nullptr, hudFlags);

        const char* text = "";
        ImVec4 color(1, 1, 1, alpha);
        switch (popup.result) {
        case gameplay::JudgmentResult::Hit300:
            text = "300";
            color = ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, alpha);
            break;
        case gameplay::JudgmentResult::Hit100:
            text = "100";
            color = ImVec4(0.702f, 1.0f, 0.0f, alpha);
            break;
        case gameplay::JudgmentResult::Hit50:
            text = "50";
            color = ImVec4(1.0f, 0.85f, 0.25f, alpha);
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
                              IM_COL32(0, 255, 245, 42), 10.0f * uiScale);
            dl->AddRect(ImVec2(left + 3.0f * uiScale, top + 3.0f * uiScale),
                        ImVec2(right - 3.0f * uiScale, bottom - 3.0f * uiScale),
                        IM_COL32(0, 255, 245, 150), 10.0f * uiScale, 0, 3.0f * uiScale);
            dl->AddLine(ImVec2(cellX, top + 8.0f * uiScale), ImVec2(cellX, bottom - 8.0f * uiScale),
                        IM_COL32(255, 255, 255, 110), 2.0f * uiScale);
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
        const float keyH = 44.0f * uiScale;
        const float keyHintY = std::min(matrixBottom + 8.0f * uiScale, H - 58.0f * uiScale - keyH);
        const int32_t startCol = m_scrollWindow.startCol;
        // 动态偏移：活跃窗口宽度算中心偏移（不再硬编码1.5）
        int32_t activeWidth = m_scrollWindow.endCol - m_scrollWindow.startCol + 1;
        float noteCenterOffset = (activeWidth - 1) * 0.5f;

        for (const auto& m : mapping) {
            // 按键固定在屏幕中央活跃列：col=startCol+0..(activeWidth-1) 居中
            float cellX = W * 0.5f + (m.column - startCol - noteCenterOffset) * gw;
            float keyW = std::min(gw * 0.8f, 80.0f * uiScale);
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

            ImGui::Begin(windowId, nullptr, fixedFlags);

            // 绘制按键背景
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            const float inset = 2.0f * uiScale;
            const float round = 6.0f * uiScale;
            ImU32 bgColor = pressed ? IM_COL32(0, 255, 245, 190) : IM_COL32(20, 20, 40, 160);
            ImU32 borderColor = pressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(50, 50, 70, 180);
            dl->AddRectFilled(ImVec2(wp.x + inset, wp.y + inset),
                              ImVec2(wp.x + keyW - inset, wp.y + keyH - inset), bgColor, round);
            dl->AddRect(ImVec2(wp.x + inset, wp.y + inset),
                        ImVec2(wp.x + keyW - inset, wp.y + keyH - inset),
                        borderColor, round, 0, pressed ? 4.0f * uiScale : 1.0f * uiScale);

            // 按键文字
            ImU32 textColor = pressed ? IM_COL32(10, 20, 35, 255) : IM_COL32(140, 140, 170, 220);
            ImGui::SetWindowFontScale(1.6f);
            float textWidth = ImGui::CalcTextSize(label).x;
            dl->AddText(ImVec2(wp.x + keyW / 2 - textWidth / 2, wp.y + 12.0f * uiScale), textColor, label);

            // ── 锁判定视觉提示：滚动/非 SCALE_ONLY 变换期间显示红色叉号 ──
            const bool judgmentLocked = m_scrollWindow.scrolling ||
                isFormationJudgmentBlocked(Kernel::instance().clock().interpolatedNowMs());
            if (judgmentLocked) {
                ImU32 xColor = IM_COL32(255, 40, 60, 255);
                const float xPad = 6.0f * uiScale;
                const float xThick = 3.0f * uiScale;
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

    popHudChrome();
} // renderImGuiOverlay()

} // namespace melody_matrix::core

#include "playing_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/result_state.h"
#include "ui/theme.h"
#include "util/logger.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/beatmap_builder.h"
#include "beatmap/mma_parser.h"
#include "platform/file_system.h"

#include "imgui.h"
#include <fstream>
#include <sstream>

namespace melody_matrix::core {

void PlayingState::onEnter() {
    MM_LOG_INFO("Playing", "Entering Playing state");

    // If we need to reinit (retry), reset everything first
    if (m_needsReinit && m_gameplayInitialized) {
        resetGameplay();
    }

    // If already initialized (resuming from pause), just resume audio
    if (m_gameplayInitialized) {
        m_audio.resume();
        MM_LOG_INFO("Playing", "Resuming playback");
        return;
    }

    initGameplay();
}

void PlayingState::onExit() {
    MM_LOG_INFO("Playing", "Exiting Playing state");
    // Only pause audio — don't stop/shutdown so we can resume later.
    // Full cleanup happens in resetGameplay() or when the state is destroyed.
    if (m_audio.isPlaying()) {
        m_audio.pause();
    }
}

void PlayingState::resetGameplay() {
    MM_LOG_INFO("Playing", "Resetting gameplay for retry");

    // Stop and cleanup current audio
    m_audio.stop();
    m_audio.shutdown();

    // Reset game state
    m_gameplayInitialized = false;
    m_songFinished = false;
    m_playerDied = false;
    m_totalNotes = 0;
    m_hitNotes = 0;
    m_perfectCount = 0;
    m_goodCount = 0;
    m_missCount = 0;
    m_needsReinit = false;

    // Note: m_beatmapFile is preserved for reinit
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

    // ── Load beatmap ──
    auto parser = beatmap::createParserForFile(m_beatmapFile);
    if (!parser) {
        MM_LOG_ERROR("Playing", "No parser available for: " + m_beatmapFile);
        return;
    }

    // Read file content
    std::ifstream ifs(m_beatmapFile);
    if (!ifs.is_open()) {
        MM_LOG_ERROR("Playing", "Cannot open file: " + m_beatmapFile);
        return;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    // Parse into builder
    beatmap::BeatmapBuilder builder;
    auto parseResult = parser->parse(content, builder);
    if (!parseResult.ok()) {
        MM_LOG_ERROR("Playing", "Failed to parse beatmap: " + parseResult.error().message);
        return;
    }

    // Build and validate
    auto buildResult = builder.build();
    if (!buildResult.ok()) {
        MM_LOG_ERROR("Playing", "Beatmap validation failed: " + buildResult.error().message);
        return;
    }
    m_beatmap = std::move(buildResult.value());

    // ── Init judge ──
    m_judgeQueue.setStrategy(std::make_unique<gameplay::StandardJudgeStrategy>());
    m_judgeQueue.loadNotes(m_beatmap.notes);

    // ── Init formation ──
    m_formationCtrl.load(m_beatmap.formations);

    // ── Count notes ──
    m_totalNotes = static_cast<int>(m_beatmap.notes.size());

    // ── Start playback ──
    if (!m_audio.playSong(m_beatmap.meta.audioFile)) {
        MM_LOG_WARN("Playing", "Failed to play: " + m_beatmap.meta.audioFile);
        // Try the full path
        auto resolved = platform::FileSystem::safeResolve("assets", m_beatmap.meta.audioFile);
        if (resolved.ok()) {
            if (!m_audio.playSong(resolved.value())) {
                MM_LOG_WARN("Playing", "Audio playback failed - continuing without audio");
            }
        }
    }

    m_gameplayInitialized = true;
    m_needsReinit = false;
    MM_LOG_INFO("Playing", "Gameplay initialized - " + std::to_string(m_totalNotes) + " notes loaded");
}

GameState PlayingState::update(float dt) {
    if (!m_gameplayInitialized) return GameState::Count;

    // ── Sync clock from audio ──
    auto& kernel = Kernel::instance();
    kernel.clock().syncFromAudio(m_audio.positionMs());

    int64_t nowMs = kernel.clock().nowMs();
    float od = m_beatmap.difficulty.od;

    // ── Process input ──
    processInput();

    // ── Update judge queue (auto-miss expired notes) ──
    m_judgeQueue.update(nowMs, od);

    // ── Update formation ──
    m_formationCtrl.update(nowMs);

    // ── HP passive drain ──
    m_hpManager.drainPerFrame(dt);

    // ── Check song end ──
    if (!m_audio.isPlaying() && m_audio.positionMs() > 0) {
        m_songFinished = true;
    }

    // ── Check HP death ──
    if (m_hpManager.isDead()) {
        m_playerDied = true;
    }

    // ── State transitions ──
    if (m_songFinished || m_playerDied) {
        // Pass score data to ResultState before transitioning
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

void PlayingState::processInput() {
    auto& kernel = Kernel::instance();
    int64_t nowMs = kernel.clock().nowMs();
    float od = m_beatmap.difficulty.od;

    // Simple key mapping: D=0, F=1, J=2, K=3
    static const int32_t keyMap[] = { SDLK_d, SDLK_f, SDLK_j, SDLK_k };

    for (int row = 0; row < 4; ++row) {
        const Uint8* state = SDL_GetKeyboardState(nullptr);
        SDL_Scancode scancode = SDL_GetScancodeFromKey(keyMap[row]);

        // Simple edge detection — check if key is down this frame
        // (In a full implementation, we'd track previous state for proper edge detection)
        if (state[scancode]) {
            auto result = m_judgeQueue.onKeyPress(nowMs, od);

            switch (result) {
            case gameplay::JudgmentResult::Perfect:
                m_perfectCount++;
                m_hitNotes++;
                m_comboManager.onHit();
                m_scoreManager.addScore(gameplay::JudgmentResult::Perfect, m_comboManager.current());
                m_hpManager.onJudgment(gameplay::JudgmentResult::Perfect);
                break;
            case gameplay::JudgmentResult::Good:
                m_goodCount++;
                m_hitNotes++;
                m_comboManager.onHit();
                m_scoreManager.addScore(gameplay::JudgmentResult::Good, m_comboManager.current());
                m_hpManager.onJudgment(gameplay::JudgmentResult::Good);
                break;
            case gameplay::JudgmentResult::Miss:
                m_missCount++;
                m_comboManager.onMiss();
                m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
                break;
            case gameplay::JudgmentResult::Ignored:
                break;
            }
        }
    }
}

void PlayingState::render() {
    if (!m_gameplayInitialized) return;

    renderHUD();
    renderImGuiOverlay();
}

void PlayingState::renderHUD() {
    // [TODO] OpenGL HUD rendering (Combo/Score/HP bars)
    // Currently handled via ImGui overlay
}

void PlayingState::renderImGuiOverlay() {
    using namespace ui; // for Theme constants

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoInputs;

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

    // Combo
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

    // ── Center: Pause hint ──
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 100,
                                    ImGui::GetIO().DisplaySize.y - 90));
    ImGui::Begin("##PauseHint", nullptr, flags);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
    ImGui::Text("ESC to pause | D F J K to play");
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace melody_matrix::core

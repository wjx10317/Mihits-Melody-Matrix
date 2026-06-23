#include "paused_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"

namespace melody_matrix::core {

void PausedState::onEnter() {
    MM_LOG_INFO("Paused", "Game paused");
    m_action = PausedAction::None;
}

void PausedState::onExit() {
    MM_LOG_INFO("Paused", "Unpausing");
}

GameState PausedState::update(float /*dt*/) {
    switch (m_action) {
    case PausedAction::Resume:
        return GameState::Playing;
    case PausedAction::Retry:
        // Mark PlayingState for reinit before transitioning
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
            }
        }
        return GameState::Playing;
    case PausedAction::Quit:
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
                // 直接清除渲染资源，因为 PlayingState::onExit() 不会被调用
                playing->cleanupRenderer();
            }
        }
        return GameState::SongSelect;
    default:
        break;
    }
    return GameState::Count;
}

void PausedState::render() {
    renderImGuiOverlay();
}

void PausedState::renderImGuiOverlay() {
    using namespace ui; // for Theme constants

    // Dim background overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.6f));
    ImGui::Begin("##PauseOverlay", nullptr, flags);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Center dialog ──
    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x / 2,
                            ImGui::GetIO().DisplaySize.y / 2);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 280));

    ImGuiWindowFlags dialogFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::Begin("##PauseDialog", nullptr, dialogFlags);

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(1.8f);
    ImGui::Text("PAUSED");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Resume button
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.5f));
    if (ImGui::Button("RESUME", ImVec2(300, 44))) {
        m_action = PausedAction::Resume;
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Retry button
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.5f));
    if (ImGui::Button("RETRY", ImVec2(300, 44))) {
        m_action = PausedAction::Retry;
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Quit button
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.5f));
    if (ImGui::Button("QUIT", ImVec2(300, 44))) {
        m_action = PausedAction::Quit;
    }
    ImGui::PopStyleColor(2);

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace melody_matrix::core

#include "result_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"
#include <cmath>

namespace melody_matrix::core {

void ResultState::onEnter() {
    MM_LOG_INFO("Result", "Entering Result state");
    m_action = ResultAction::None;
}

void ResultState::onExit() {
    MM_LOG_INFO("Result", "Exiting Result state");
}

GameState ResultState::update(float dt) {
    switch (m_action) {
    case ResultAction::Retry:
        // Mark PlayingState for reinit before transitioning
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
            }
        }
        return GameState::Playing;
    case ResultAction::Back:
        return GameState::SongSelect;
    default:
        break;
    }
    return GameState::Count;
}

void ResultState::render() {
    renderImGuiPanel();
}

void ResultState::renderImGuiPanel() {
    using namespace ui; // for Theme constants

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##ResultScreen", nullptr, flags);

    float centerX = displaySize.x * 0.5f;
    float contentWidth = 600.0f;
    float startX = centerX - contentWidth * 0.5f;

    // ── Title ──
    ImGui::SetCursorPosX(startX);
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(2.5f);
    const char* titleText = playerDied ? "GAME OVER" : "RESULT";
    ImGui::Text(titleText);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    if (!songTitle.empty()) {
        ImGui::SetCursorPosX(startX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
        ImGui::Text("%s", songTitle.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Score ──
    ImGui::SetCursorPosX(startX);
    ImGui::SetWindowFontScale(3.0f);
    if (playerDied) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    }
    ImGui::Text("%08d", score);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Spacing();

    // ── Accuracy ──
    float accuracy = totalNotes > 0 ?
        (perfectCount * 100.0f + goodCount * 60.0f) / (totalNotes * 100.0f) * 100.0f : 0.0f;
    ImGui::SetCursorPosX(startX);
    ImGui::Text("Accuracy: %.2f%%", accuracy);
    ImGui::SetCursorPosX(startX);
    ImGui::Text("Max Combo: %dx", maxCombo);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Judgment breakdown ──
    ImGui::SetCursorPosX(startX);
    ImGui::Columns(3, "##judgeCols", false);
    ImGui::SetColumnWidth(0, 200);
    ImGui::SetColumnWidth(1, 200);
    ImGui::SetColumnWidth(2, 200);

    // Perfect
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", perfectCount);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("PERFECT");
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    // Good
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.702f, 1.0f, 0.0f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", goodCount);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("GOOD");
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    // Miss
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", missCount);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("MISS");
    ImGui::PopStyleColor();

    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Grade ──
    if (!playerDied) {
        const char* grade = "D";
        if (accuracy >= 95.0f) grade = "S";
        else if (accuracy >= 90.0f) grade = "A";
        else if (accuracy >= 80.0f) grade = "B";
        else if (accuracy >= 70.0f) grade = "C";

        ImGui::SetCursorPosX(startX);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
        ImGui::SetWindowFontScale(4.0f);
        ImGui::Text("%s", grade);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Buttons ──
    float btnWidth = 200;
    float btnHeight = 44;
    float btnTotalWidth = btnWidth * 2 + 20;
    float btnStartX = centerX - btnTotalWidth * 0.5f;

    ImGui::SetCursorPosX(btnStartX);
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.5f));
    if (ImGui::Button("RETRY", ImVec2(btnWidth, btnHeight))) {
        m_action = ResultAction::Retry;
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::BG_R + 0.04f, Theme::BG_G + 0.04f, Theme::BG_B + 0.06f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::BG_R + 0.08f, Theme::BG_G + 0.08f, Theme::BG_B + 0.1f, 1.0f));
    if (ImGui::Button("BACK", ImVec2(btnWidth, btnHeight))) {
        m_action = ResultAction::Back;
    }
    ImGui::PopStyleColor(2);

    ImGui::End();
}

} // namespace melody_matrix::core

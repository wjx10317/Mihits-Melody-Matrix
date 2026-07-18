// ============================================================
// result_state.cpp — 结算状态实现
//
// 职责：
//   - 展示分数、准确度、连击与判定统计
//   - 根据准确度计算评级（S/A/B/C/D）
//   - 提供重试与返回选歌操作
// ============================================================
#include "result_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "platform/config.h"
#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"
#include <cmath>
#include <string>
namespace melody_matrix::core {

// ══════════════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════════════

/// 进入结算状态
void ResultState::onEnter() {
    MM_LOG_INFO("Result", "Entering Result state");
    m_action = ResultAction::None;
}

/// 退出结算状态
void ResultState::onExit() {
    MM_LOG_INFO("Result", "Exiting Result state");
}

/// 根据用户选择决定下一状态（Retry→Playing reinit，Back→SongSelect 并清渲染）
GameState ResultState::update(float /*dt*/) {
    switch (m_action) {
    case ResultAction::Retry:
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
            }
        }
        return GameState::Playing;
    case ResultAction::Back:
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
                playing->cleanupRenderer();                    // 返回选歌前释放 gameplay GL 资源
            }
        }
        return GameState::SongSelect;
    default:
        break;
    }
    return GameState::Count;
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染结算 UI
void ResultState::render() {
    renderImGuiPanel();
}

/// 绘制全屏结算面板：标题、分数、准确度、判定统计、评级与操作按钮
void ResultState::renderImGuiPanel() {
    using namespace ui; // Theme 颜色常量

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

    const float s = Theme::displayScale();
    float centerX = displaySize.x * 0.5f;
    float contentWidth = 600.0f * s;
    float startX = centerX - contentWidth * 0.5f;

    // ── 标题 ──
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

    // ── 分数 ──
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

    // ── 准确度（Stable：300/100/50 加权）──
    float accuracy = totalNotes > 0 ?
        (hit300Count * 300.0f + hit100Count * 100.0f + hit50Count * 50.0f) /
            (totalNotes * 300.0f) * 100.0f : 0.0f;
    ImGui::SetCursorPosX(startX);
    ImGui::Text("Accuracy: %.2f%%", accuracy);
    ImGui::SetCursorPosX(startX);
    ImGui::Text("Max Combo: %dx", maxCombo);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 判定统计 ──
    ImGui::SetCursorPosX(startX);
    ImGui::Columns(4, "##judgeCols", false);
    ImGui::SetColumnWidth(0, 150);
    ImGui::SetColumnWidth(1, 150);
    ImGui::SetColumnWidth(2, 150);
    ImGui::SetColumnWidth(3, 150);

    // 300
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", hit300Count);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("300");
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    // 100
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.702f, 1.0f, 0.0f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", hit100Count);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("100");
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    // 50
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("%d", hit50Count);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("50");
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

    // ── 击打偏移中位数 → 推荐 Timing Offset ──
    if (hitOffsetSampleCount > 0) {
        ImGui::SetCursorPosX(startX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.78f, 1.0f));
        ImGui::Text("Hit offset median: %+lld ms  (n=%d)",
                    static_cast<long long>(hitOffsetMedianMs), hitOffsetSampleCount);
        ImGui::SetCursorPosX(startX);
        ImGui::Text("Timing offset: %+lld → recommend %+lld ms",
                    static_cast<long long>(currentTimingOffsetMs),
                    static_cast<long long>(recommendedTimingOffsetMs));
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::SetCursorPosX(startX);
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.55f));
        if (ImGui::Button("APPLY OFFSET", ImVec2(220.0f * s, 36.0f * s))) {
            const int clamped = static_cast<int>(std::max<int64_t>(
                -100, std::min<int64_t>(100, recommendedTimingOffsetMs)));
            platform::Config::setInt(platform::Config::KEY_TIMING_OFFSET, clamped);
            platform::Config::save();
            currentTimingOffsetMs = clamped;
            recommendedTimingOffsetMs = clamped;
            MM_LOG_INFO("Result", "Applied timing offset=" + std::to_string(clamped) + " ms");
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ── 评级 ──
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

    // ── 操作按钮 ──
    float btnWidth = 200.0f * s;
    float btnHeight = 44.0f * s;
    float btnTotalWidth = btnWidth * 2 + 20.0f * s;
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

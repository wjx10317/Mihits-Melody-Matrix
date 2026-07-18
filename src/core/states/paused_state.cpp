// ============================================================
// paused_state.cpp — 暂停覆盖层状态实现
//
// 职责：
//   - 渲染半透明遮罩与居中暂停对话框
//   - 处理继续 / 重试 / 退出三种用户操作
//   - 退出时清理 PlayingState 渲染资源（因 onExit 不会被调用）
// ============================================================
#include "paused_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════════════

/// 进入暂停状态，重置待执行动作
void PausedState::onEnter() {
    MM_LOG_INFO("Paused", "Game paused");
    m_action = PausedAction::None;
}

/// 退出暂停状态
void PausedState::onExit() {
    MM_LOG_INFO("Paused", "Unpausing");
}

/// 根据用户选择的动作决定下一状态（Resume/Retry→Playing，Quit→SongSelect）
GameState PausedState::update(float /*dt*/) {
    switch (m_action) {
    case PausedAction::Resume:
        return GameState::Playing;                           // 直接恢复，不 reinit
    case PausedAction::Retry:
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();                  // 重试需 resetGameplay + initGameplay
            }
        }
        return GameState::Playing;
    case PausedAction::Quit:
        {
            auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
            if (playing) {
                playing->markNeedsReinit();
                playing->cleanupRenderer();                    // Paused 覆盖层下 Playing::onExit 不调
            }
        }
        return GameState::SongSelect;
    default:
        break;                                               // None：保持暂停
    }
    return GameState::Count;
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染暂停 UI
void PausedState::render() {
    renderImGuiOverlay();
}

/// 绘制半透明全屏遮罩与居中暂停对话框（继续 / 重试 / 退出）
void PausedState::renderImGuiOverlay() {
    using namespace ui; // Theme 颜色常量

    // 半透明背景遮罩
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

    // ── 居中对话框 ──
    const float s = Theme::displayScale();
    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x / 2,
                            ImGui::GetIO().DisplaySize.y / 2);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360.0f * s, 280.0f * s));

    ImGuiWindowFlags dialogFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * s, 20.0f * s));
    ImGui::Begin("##PauseDialog", nullptr, dialogFlags);

    // 标题
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(1.8f);
    ImGui::Text("PAUSED");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 继续按钮
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.5f));
    if (ImGui::Button("RESUME", ImVec2(300.0f * s, 44.0f * s))) {
        m_action = PausedAction::Resume;
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // 重试按钮
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.5f));
    if (ImGui::Button("RETRY", ImVec2(300.0f * s, 44.0f * s))) {
        m_action = PausedAction::Retry;
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // 退出按钮
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.5f));
    if (ImGui::Button("QUIT", ImVec2(300.0f * s, 44.0f * s))) {
        m_action = PausedAction::Quit;
    }
    ImGui::PopStyleColor(2);

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace melody_matrix::core

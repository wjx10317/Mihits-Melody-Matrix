/**
 * @file scene_transition.cpp
 * @brief SceneTransition 实现
 *
 * 文件职责：
 *   实现线性 alpha 插值及 ImGui 全屏无输入遮罩窗口绘制。
 *
 * 主要依赖：
 *   scene_transition.h、imgui.h。
 *
 * 在项目中的用法：
 *   仅由 StateManager 调用，不对外直接使用。
 */
#include "scene_transition.h"
#include "imgui.h"

#include <algorithm>

namespace melody_matrix::core {

void SceneTransition::startFadeOut(float duration) {
    m_phase = Phase::FadingOut;
    m_duration = duration;
    m_elapsed = 0.0f;
    m_alpha = 0.0f;
}

void SceneTransition::startFadeIn(float duration) {
    m_phase = Phase::FadingIn;
    m_duration = duration;
    m_elapsed = 0.0f;
    m_alpha = 1.0f;
}

bool SceneTransition::active() const {
    return m_phase != Phase::None;
}

bool SceneTransition::fadeOutComplete() const {
    return m_phase == Phase::FadingOut && m_elapsed >= m_duration;
}

void SceneTransition::update(float dt) {
    if (m_phase == Phase::None) return;

    m_elapsed += dt;

    if (m_phase == Phase::FadingOut) {
        // alpha 线性 0 → 1
        float t = std::min(m_elapsed / m_duration, 1.0f);
        m_alpha = t;
        if (t >= 1.0f) {
            m_alpha = 1.0f;
            // 保持 FadingOut 阶段直到 StateManager 切换状态并 startFadeIn
        }
    } else if (m_phase == Phase::FadingIn) {
        // alpha 线性 1 → 0
        float t = std::min(m_elapsed / m_duration, 1.0f);
        m_alpha = 1.0f - t;
        if (t >= 1.0f) {
            m_alpha = 0.0f;
            m_phase = Phase::None;
        }
    }
}

void SceneTransition::render() {
    if (m_phase == Phase::None || m_alpha <= 0.001f) return;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 全屏透明窗口，仅用 WindowBg alpha 绘制黑幕
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, m_alpha));
    ImGui::Begin("##SceneTransition", nullptr, flags);
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace melody_matrix::core

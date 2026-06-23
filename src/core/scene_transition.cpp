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
        // alpha: 0 → 1
        float t = std::min(m_elapsed / m_duration, 1.0f);
        m_alpha = t;
        if (t >= 1.0f) {
            m_alpha = 1.0f;
            // FadingOut 完成后等待外部切换状态并触发 FadingIn
        }
    } else if (m_phase == Phase::FadingIn) {
        // alpha: 1 → 0
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

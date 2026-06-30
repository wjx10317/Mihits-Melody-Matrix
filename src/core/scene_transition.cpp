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
#include "scene_transition.h"  // SceneTransition 类声明
#include "imgui.h"             // ImGui 全屏遮罩窗口绘制

#include <algorithm>  // std::min：将插值系数 clamp 到 [0,1]

namespace melody_matrix::core {

void SceneTransition::startFadeOut(float duration) {
    m_phase = Phase::FadingOut;  // 进入淡出阶段
    m_duration = duration;         // 记录本阶段动画时长
    m_elapsed = 0.0f;              // 重置已用时间
    m_alpha = 0.0f;                // 从完全透明开始变黑
}

void SceneTransition::startFadeIn(float duration) {
    m_phase = Phase::FadingIn;  // 进入淡入阶段（状态切换完成后调用）
    m_duration = duration;
    m_elapsed = 0.0f;
    m_alpha = 1.0f;  // 从全黑开始渐显新场景
}

bool SceneTransition::active() const {
    return m_phase != Phase::None;  // 非 None 即过渡进行中
}

bool SceneTransition::fadeOutComplete() const {
    // 淡出阶段且已跑满时长 → StateManager 可执行 executeTransition
    return m_phase == Phase::FadingOut && m_elapsed >= m_duration;
}

void SceneTransition::update(float dt) {
    // 无过渡时无需更新 alpha
    if (m_phase == Phase::None) return;

    m_elapsed += dt;  // 累加本帧经过时间

    if (m_phase == Phase::FadingOut) {
        // alpha 线性 0 → 1
        float t = std::min(m_elapsed / m_duration, 1.0f);  // 归一化进度 [0,1]
        m_alpha = t;
        if (t >= 1.0f) {
            m_alpha = 1.0f;
            // 保持 FadingOut 阶段直到 StateManager 切换状态并 startFadeIn
        }
    } else if (m_phase == Phase::FadingIn) {
        // alpha 线性 1 → 0
        float t = std::min(m_elapsed / m_duration, 1.0f);  // 淡入进度
        m_alpha = 1.0f - t;  // 黑幕逐渐透明
        if (t >= 1.0f) {
            m_alpha = 0.0f;
            m_phase = Phase::None;  // 淡入完成，过渡结束
        }
    }
}

void SceneTransition::render() {
    // 无过渡或几乎透明时不绘制，避免多余 ImGui 窗口
    if (m_phase == Phase::None || m_alpha <= 0.001f) return;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;  // 获取当前 ImGui 视口物理尺寸

    // 全屏透明窗口，仅用 WindowBg alpha 绘制黑幕
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);       // 固定左上角
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);       // 铺满整个显示区域

    // 无标题、不可交互、不抢焦点，纯遮罩层
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, m_alpha));  // 黑色背景，alpha 控制不透明度
    ImGui::Begin("##SceneTransition", nullptr, flags);  // 隐藏标题的全屏窗口
    ImGui::End();
    ImGui::PopStyleColor();  // 恢复默认 WindowBg 颜色
}

} // namespace melody_matrix::core

#pragma once

// 全状态共用的右下角帧统计 overlay（ImGui ForegroundDrawList）

struct SDL_Window;

namespace melody_matrix::ui {

/// 右下角双框：上 = 实际/理论刷新率（Hz），下 = 渲染帧间隔 ms
class FrameStatsOverlay {
public:
    /// 查询窗口所在显示器的刷新率（Hz）；失败时默认 60
    void init(SDL_Window* window);

    /// 每显示帧调用一次，传入与上一显示帧的 wall-clock 间隔（毫秒）
    void setFrameTimeMs(float frameTimeMs);

    /// 在 ImGui::NewFrame 之后、Render 之前绘制
    void render() const;

private:
    float m_displayRefreshHz = 60.0f;
    float m_frameTimeMs = 0.0f;
    bool m_initialized = false;
};

} // namespace melody_matrix::ui

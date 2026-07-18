#include "ui/frame_stats_overlay.h"

#include "imgui.h"

#include <SDL.h>

#include <cstdio>

namespace melody_matrix::ui {

void FrameStatsOverlay::init(SDL_Window* window) {
    m_displayRefreshHz = 60.0f;
    if (window) {
        const int displayIndex = SDL_GetWindowDisplayIndex(window);
        if (displayIndex >= 0) {
            SDL_DisplayMode mode{};
            if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0 && mode.refresh_rate > 0) {
                m_displayRefreshHz = static_cast<float>(mode.refresh_rate);
            }
        }
    }
    m_initialized = true;
}

void FrameStatsOverlay::setFrameTimeMs(float frameTimeMs) {
    m_frameTimeMs = frameTimeMs;
}

void FrameStatsOverlay::render() const {
    if (!m_initialized) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    // 相对 1080p 缩放：布局与字号都跟当前分辨率走
    const float scale = io.DisplaySize.y > 0.0f ? io.DisplaySize.y / 1080.0f : 1.0f;
    const float pad = 12.0f * scale;
    const float boxW = 112.0f * scale;
    const float boxH = 26.0f * scale;
    const float gap = 6.0f * scale;
    const float rounding = 6.0f * scale;

    const float x0 = io.DisplaySize.x - pad - boxW;
    const float yBottom = io.DisplaySize.y - pad - boxH;
    const float yTop = yBottom - gap - boxH;

    // 恢复绿色调试风格
    const ImU32 fill = IM_COL32(16, 48, 28, 210);
    const ImU32 border = IM_COL32(40, 220, 100, 255);
    const ImU32 textCol = IM_COL32(180, 255, 200, 255);

    const float actualHz = (m_frameTimeMs > 0.001f) ? (1000.0f / m_frameTimeMs) : 0.0f;

    char topText[48];
    char bottomText[32];
    if (actualHz > 0.001f) {
        std::snprintf(topText, sizeof(topText), "%.2f/%.0f",
                      actualHz, m_displayRefreshHz);
    } else {
        std::snprintf(topText, sizeof(topText), "---/%.0f", m_displayRefreshHz);
    }
    std::snprintf(bottomText, sizeof(bottomText), "%.2f ms", m_frameTimeMs);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    // 字号按分辨率缩放（不依赖启动时 atlas 的绝对像素）
    const float fontSize = 16.5f * scale;

    auto drawBox = [&](float y, const char* text) {
        const ImVec2 p0(x0, y);
        const ImVec2 p1(x0 + boxW, y + boxH);
        dl->AddRectFilled(p0, p1, fill, rounding);
        dl->AddRect(p0, p1, border, rounding, 0, 1.5f * scale);

        const ImVec2 textSize = font->CalcTextSizeA(fontSize, boxW, 0.0f, text);
        const ImVec2 textPos(
            x0 + (boxW - textSize.x) * 0.5f,
            y + (boxH - textSize.y) * 0.5f);
        dl->AddText(font, fontSize, textPos, textCol, text);
    };

    drawBox(yTop, topText);
    drawBox(yBottom, bottomText);
}

} // namespace melody_matrix::ui

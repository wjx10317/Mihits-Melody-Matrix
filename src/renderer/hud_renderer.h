#pragma once

#include "renderer/shader.h"
#include <cstdint>

namespace melody_matrix::renderer {

/// HUD 渲染器 — 使用 OpenGL 绘制连击/分数/HP（非 ImGui）。
/// 根据设计文档 9.3：HUD 通过 OpenGL 而非 ImGui 绘制，
/// 以获得更好的视觉效果和统一的渲染管线。
class HudRenderer {
public:
    HudRenderer() = default;
    ~HudRenderer() = default;

    bool init();
    void render(int64_t score, int32_t combo, float hp, float accuracy);
    void shutdown();

private:
    void renderScoreNumber(int64_t score, float x, float y, float scale);
    void renderComboNumber(int32_t combo, float x, float y, float scale);
    void renderHPBar(float hp, float x, float y, float width, float height);

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    Shader m_shader;
};

} // namespace melody_matrix::renderer

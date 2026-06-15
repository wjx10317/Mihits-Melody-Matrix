#pragma once

#include "renderer/shader.h"
#include <cstdint>

namespace melody_matrix::renderer {

/// 边框渲染器 — 在网格周围绘制收缩边框效果。
/// 边框随着音符接近从网格边缘向音符收缩。
/// 收缩速度由 AR（接近速率）控制。
class BorderRenderer {
public:
    BorderRenderer() = default;
    ~BorderRenderer() = default;

    bool init();
    void render(int64_t timeMs, float borderScale, int rows, int cols);
    void shutdown();

private:
    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    Shader m_shader;
};

} // namespace melody_matrix::renderer

#include "border_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace melody_matrix::renderer {

bool BorderRenderer::init() {
    MM_LOG_INFO("BorderRenderer", "Initializing...");

    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        m_shader = std::move(result.value());
    } else {
        MM_LOG_WARN("BorderRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    m_initialized = true;
    MM_LOG_INFO("BorderRenderer", "Initialized");
    return true;
}

void BorderRenderer::render(int64_t timeMs, float borderScale, int rows, int cols) {
    if (!m_initialized) return;

    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / cols;
    const float gh = (H - 2 * margin) / rows;

    // Full grid bounds
    float left = margin;
    float right = W - margin;
    float top = margin;
    float bottom = H - margin;

    // Shrunk bounds based on borderScale (1.0 = full, 0.0 = collapsed)
    float s = 1.0f - borderScale; // Invert: 0 = full, 1 = collapsed
    float inset = s * 40.0f;      // Max inset of 40px when fully collapsed

    float l = left + inset;
    float r = right - inset;
    float t = top + inset;
    float b = bottom - inset;

    // Draw four border lines as a rectangle outline
    std::vector<float> lines = {
        l, t,  r, t,   // top
        r, t,  r, b,   // right
        r, b,  l, b,   // bottom
        l, b,  l, t    // left
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);

    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, W, H, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    // Border color: hot pink at approach, dimmer when far
    float alpha = 0.3f + 0.5f * s;
    m_shader.setVec4("uColor", 1.0f, 0.0f, 0.431f, alpha);

    glBindVertexArray(m_vao);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, 8);
    glBindVertexArray(0);
}

void BorderRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_initialized = false;
}

} // namespace melody_matrix::renderer

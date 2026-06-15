#include "hud_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

namespace melody_matrix::renderer {

bool HudRenderer::init() {
    MM_LOG_INFO("HudRenderer", "Initializing...");

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
        MM_LOG_WARN("HudRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 64, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    m_initialized = true;
    MM_LOG_INFO("HudRenderer", "Initialized");
    return true;
}

void HudRenderer::render(int64_t score, int32_t combo, float hp, float accuracy) {
    if (!m_initialized) return;

    renderHPBar(hp, 20.0f, 1040.0f, 1880.0f, 20.0f);
    // Score and combo are currently rendered via ImGui overlay for simplicity.
    // Full OpenGL text rendering would require a font atlas.
}

void HudRenderer::renderHPBar(float hp, float x, float y, float width, float height) {
    hp = std::max(0.0f, std::min(1.0f, hp));

    // Background
    std::vector<float> bgVerts = {
        x, y,             x + width, y,
        x + width, y + height,  x, y + height
    };

    // Foreground (filled portion)
    float filledWidth = width * hp;
    std::vector<float> fgVerts = {
        x, y,                   x + filledWidth, y,
        x + filledWidth, y + height,  x, y + height
    };

    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    // Draw background
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, bgVerts.size() * sizeof(float), bgVerts.data(), GL_DYNAMIC_DRAW);
    m_shader.setVec4("uColor", 0.14f, 0.14f, 0.22f, 0.8f);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Draw foreground
    glm::vec4 hpColor;
    if (hp > 0.5f) {
        hpColor = glm::vec4(0.0f, 1.0f, 0.96f, 0.9f);  // Cyan
    } else if (hp > 0.25f) {
        hpColor = glm::vec4(0.94f, 0.62f, 0.15f, 0.9f); // Amber
    } else {
        hpColor = glm::vec4(1.0f, 0.0f, 0.431f, 0.9f);  // Pink
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, fgVerts.size() * sizeof(float), fgVerts.data(), GL_DYNAMIC_DRAW);
    m_shader.setVec4("uColor", hpColor.r, hpColor.g, hpColor.b, hpColor.a);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Border
    float b = 1.0f;
    std::vector<float> borderLines = {
        x, y,  x + width, y,
        x + width, y,  x + width, y + height,
        x + width, y + height,  x, y + height,
        x, y + height,  x, y
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, borderLines.size() * sizeof(float), borderLines.data(), GL_DYNAMIC_DRAW);
    m_shader.setVec4("uColor", 0.3f, 0.3f, 0.4f, 0.6f);
    glLineWidth(1.5f);
    glDrawArrays(GL_LINES, 0, 8);

    glBindVertexArray(0);
}

void HudRenderer::renderScoreNumber(int64_t, float, float, float) {
    // [TODO] Requires font atlas for proper OpenGL text rendering
}

void HudRenderer::renderComboNumber(int32_t, float, float, float) {
    // [TODO] Requires font atlas for proper OpenGL text rendering
}

void HudRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_initialized = false;
}

} // namespace melody_matrix::renderer

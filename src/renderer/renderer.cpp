#include "renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace melody_matrix::renderer {

bool Renderer::init() {
    MM_LOG_INFO("Renderer", "Initializing renderer...");

    // ── Compile grid shader ──
    const std::string gridVertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const std::string gridFragSrc = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

    auto shaderResult = Shader::compile(gridVertSrc, gridFragSrc);
    if (shaderResult.ok()) {
        m_gridShader = std::move(shaderResult.value());
    } else {
        MM_LOG_WARN("Renderer", "Grid shader failed, using fallback: " + shaderResult.error().message);
    }

    // ── Create grid VAO/VBO ──
    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);

    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 256, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // ── Compile background shader (textured fullscreen quad) ──
    const std::string bgVertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTexCoord;
        out vec2 vTexCoord;
        uniform mat4 uProjection;
        void main() {
            vTexCoord = aTexCoord;
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const std::string bgFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uTexture;
        uniform float uAlpha;
        void main() {
            FragColor = texture(uTexture, vTexCoord) * uAlpha;
        }
    )";

    auto bgShaderResult = Shader::compile(bgVertSrc, bgFragSrc);
    if (bgShaderResult.ok()) {
        m_bgShader = std::move(bgShaderResult.value());
    } else {
        MM_LOG_WARN("Renderer", "Background shader failed: " + bgShaderResult.error().message);
    }

    // ── Create fullscreen quad VAO/VBO for background ──
    // Vertices: position (x,y) + texcoord (u,v)
    // Covers the full logical 1920×1080 area
    float quadVertices[] = {
        // pos              // texcoord
        0.0f,    0.0f,      0.0f, 0.0f,   // top-left
        1920.0f, 0.0f,      1.0f, 0.0f,   // top-right
        1920.0f, 1080.0f,   1.0f, 1.0f,   // bottom-right
        0.0f,    0.0f,      0.0f, 0.0f,   // top-left
        1920.0f, 1080.0f,   1.0f, 1.0f,   // bottom-right
        0.0f,    1080.0f,   0.0f, 1.0f,   // bottom-left
    };

    glGenVertexArrays(1, &m_bgVao);
    glGenBuffers(1, &m_bgVbo);

    glBindVertexArray(m_bgVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_bgVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // Texcoord attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // ── Load background texture ──
    if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {
        MM_LOG_WARN("Renderer", "Failed to load menu background texture — will use solid color");
    }

    m_initialized = true;
    MM_LOG_INFO("Renderer", "Renderer initialized successfully");
    return true;
}

void Renderer::setGameplayRendering(bool enabled) {
    m_gameplayRendering = enabled;
}

void Renderer::renderFrame(int64_t interpolatedTimeMs) {
    if (!m_initialized) return;

    renderBackground();
    if (m_gameplayRendering) {
        renderGrid(interpolatedTimeMs);
    }
    // [TODO] renderBorder(timeMs)
    // [TODO] renderEffects(timeMs)
}

void Renderer::renderBackground() {
    if (!m_bgTexture.valid() || !m_bgShader.valid()) {
        // Fallback: solid clear color already set by Kernel
        return;
    }

    // Bind texture
    m_bgTexture.bind(0);

    // Use background shader
    m_bgShader.use();

    // Projection: orthographic mapping logical 1920×1080 → clip space
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_bgShader.setMat4("uProjection", &proj[0][0]);

    // Texture sampler at unit 0
    m_bgShader.setInt("uTexture", 0);

    // Full opacity for background
    m_bgShader.setFloat("uAlpha", 1.0f);

    // Draw fullscreen quad
    glBindVertexArray(m_bgVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Unbind texture
    Texture2D::unbind(0);
}

void Renderer::renderGrid(int64_t timeMs) {
    // ── Build grid lines based on current formation ──
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / m_gridCols;
    const float gh = (H - 2 * margin) / m_gridRows;

    std::vector<float> lines;

    // Vertical lines (cols + 1)
    for (int c = 0; c <= m_gridCols; ++c) {
        float x = margin + c * gw;
        lines.push_back(x); lines.push_back(margin);
        lines.push_back(x); lines.push_back(H - margin);
    }

    // Horizontal lines (rows + 1)
    for (int r = 0; r <= m_gridRows; ++r) {
        float y = margin + r * gh;
        lines.push_back(margin); lines.push_back(y);
        lines.push_back(W - margin); lines.push_back(y);
    }

    int32_t vertexCount = static_cast<int32_t>(lines.size() / 2);

    // ── Update VBO ──
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);

    // ── Draw ──
    m_gridShader.use();

    // Projection: orthographic mapping logical 1920x1080 → clip space
    glm::mat4 proj = glm::ortho(0.0f, W, H, 0.0f, -1.0f, 1.0f);
    m_gridShader.setMat4("uProjection", &proj[0][0]);

    // Grid line color: dim cyan (#00fff5 at 30% opacity)
    m_gridShader.setVec4("uColor", 0.0f, 1.0f, 0.96f, 0.3f);

    glBindVertexArray(m_gridVao);
    glDrawArrays(GL_LINES, 0, vertexCount);
    glBindVertexArray(0);
}

void Renderer::shutdown() {
    // Clean up background resources
    m_bgTexture = Texture2D(); // RAII cleanup
    if (m_bgVao != 0) {
        glDeleteVertexArrays(1, &m_bgVao);
        m_bgVao = 0;
    }
    if (m_bgVbo != 0) {
        glDeleteBuffers(1, &m_bgVbo);
        m_bgVbo = 0;
    }

    // Clean up grid resources
    if (m_gridVao != 0) {
        glDeleteVertexArrays(1, &m_gridVao);
        m_gridVao = 0;
    }
    if (m_gridVbo != 0) {
        glDeleteBuffers(1, &m_gridVbo);
        m_gridVbo = 0;
    }
    m_initialized = false;
    MM_LOG_INFO("Renderer", "Renderer shut down");
}

} // namespace melody_matrix::renderer

#include "note_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace melody_matrix::renderer {

bool NoteRenderer::init() {
    MM_LOG_INFO("NoteRenderer", "Initializing...");

    // ── Compile note shader ──
    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;       // unit quad
        layout(location = 1) in vec4 aInstance;   // x, y, w, h
        layout(location = 2) in vec4 aColor;      // RGBA

        uniform mat4 uProjection;
        out vec4 vColor;

        void main() {
            vec2 worldPos = aPos * aInstance.zw + aInstance.xy;
            gl_Position = uProjection * vec4(worldPos, 0.0, 1.0);
            vColor = aColor;
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )";

    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        m_shader = std::move(result.value());
    } else {
        MM_LOG_WARN("NoteRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    // ── Create unit quad (two triangles) ──
    float quad[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);
    glGenBuffers(1, &m_instanceVbo);
    glGenBuffers(1, &m_colorVbo);

    glBindVertexArray(m_vao);

    // Quad geometry (location 0)
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Instance data (location 1): x, y, w, h
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(1, 1);

    // Instance color (location 2): RGBA
    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);

    m_initialized = true;
    MM_LOG_INFO("NoteRenderer", "Initialized successfully");
    return true;
}

void NoteRenderer::render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar) {
    if (!m_initialized) return;

    std::vector<float> quads;
    std::vector<float> colors;
    buildNoteVertices(notes, timeMs, rows, cols, ar, quads, colors);

    if (quads.empty()) return;

    int32_t instanceCount = static_cast<int32_t>(quads.size() / 4);
    instanceCount = std::min(instanceCount, m_maxInstances);

    // Update instance buffers
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, quads.size() * sizeof(float), quads.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_DYNAMIC_DRAW);

    // Draw
    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instanceCount);
    glBindVertexArray(0);
}

void NoteRenderer::buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                                      int rows, int cols, float ar,
                                      std::vector<float>& quads,
                                      std::vector<float>& colors) {
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / cols;
    const float gh = (H - 2 * margin) / rows;
    const float noteW = gw * 0.7f;
    const float noteH = gh * 0.7f;

    // Approach time based on AR (osu formula)
    float approachMs = 1800.0f - ar * 120.0f; // AR5 = 1200ms, AR10 = 600ms
    if (approachMs < 300.0f) approachMs = 300.0f;

    for (const auto& note : notes) {
        int64_t noteTime = note.time;
        float timeDiff = static_cast<float>(noteTime - timeMs);

        // Only render notes within approach window
        if (timeDiff > approachMs || timeDiff < -500.0f) continue;

        // Calculate note position
        float cellX = margin + (note.col + 0.5f) * gw;
        float cellY = margin + (note.row + 0.5f) * gh;

        // Approach scale: note grows from center as it approaches
        float approachProgress = 1.0f - (timeDiff / approachMs);
        approachProgress = std::max(0.0f, std::min(1.0f, approachProgress));

        float scale = 0.3f + 0.7f * approachProgress; // Start at 30%, grow to 100%

        if (note.type == beatmap::NoteType::Tap) {
            float x = cellX - noteW * scale * 0.5f;
            float y = cellY - noteH * scale * 0.5f;
            float w = noteW * scale;
            float h = noteH * scale;

            quads.insert(quads.end(), { x, y, w, h });

            // Color: cyan for Perfect, purple for Good area, fading in
            float alpha = approachProgress * 0.9f;
            if (approachProgress > 0.9f) {
                // Near hit time — bright cyan
                colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, alpha });
            } else {
                // Approaching — dimmer
                colors.insert(colors.end(), { 0.0f, 0.6f, 0.58f, alpha * 0.6f });
            }
        } else if (note.type == beatmap::NoteType::Hold) {
            // Hold note: draw as a bar from current position to end
            float x = cellX - noteW * 0.35f;
            float y = cellY - noteH * 0.35f;
            float w = noteW * 0.7f;
            float holdDuration = static_cast<float>(note.holdEnd - noteTime);

            // Height based on hold duration (scaled)
            float h = std::min(holdDuration * 0.5f, gh * 2.0f);

            quads.insert(quads.end(), { x, y, w, h });
            colors.insert(colors.end(), { 0.702f, 0.0f, 1.0f, 0.7f * approachProgress });
        }
    }
}

void NoteRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_quadVbo != 0) { glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_instanceVbo != 0) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_colorVbo != 0) { glDeleteBuffers(1, &m_colorVbo); m_colorVbo = 0; }
    m_initialized = false;
}

} // namespace melody_matrix::renderer

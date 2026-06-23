#include "note_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
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
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount) {
    if (!m_initialized) return;

    std::vector<float> quads;
    std::vector<float> colors;
    buildNoteVertices(notes, timeMs, rows, cols, ar,
                      activeStartCol, activeEndCol,
                      colHeads, colHeadCount, quads, colors);

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
                                      int32_t activeStartCol, int32_t activeEndCol,
                                      const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                                      std::vector<float>& quads,
                                      std::vector<float>& colors) {
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / cols;
    const float gh = (H - 2 * margin) / rows;
    // 音符满尺寸 = 格子大小 × 0.9（留 10% 间距），确保 100% 时填满格子
    const float noteFullW = gw * 0.9f;
    const float noteFullH = gh * 0.9f;

    // Approach time based on AR (osu formula)
    float approachMs = 1800.0f - ar * 120.0f;
    if (approachMs < 300.0f) approachMs = 300.0f;

    // colHeads[col] = 该列 JudgeQueue 中已前进的 head 数量
    std::array<size_t, 8> colEncounterCount = {};

    for (const auto& note : notes) {
        // 跳过已判定的音符
        if (note.col >= 0 && note.col < colHeadCount) {
            size_t& encountered = colEncounterCount[note.col];
            if (encountered < colHeads[note.col]) {
                encountered++;
                continue;
            }
            encountered++;
        }

        float timeDiff = static_cast<float>(note.time - timeMs);

        // Only render notes within approach window + brief after
        if (timeDiff > approachMs || timeDiff < -300.0f) continue;

        // 完整矩阵显示
        float cellX = margin + (note.col + 0.5f) * gw;
        float cellY = margin + (note.row + 0.5f) * gh;

        // 活跃列高亮，非活跃列灰暗
        float colDim = 1.0f;
        if (cols > 4) {
            if (note.col < activeStartCol || note.col > activeEndCol) {
                colDim = 0.25f;
            }
        }

        // ── 三阶段视觉 ──
        // Phase 1: approach (timeDiff > 0) — 音符从远处接近，判定环收缩
        // Phase 2: hittable (timeDiff ≈ 0) — 音符发光，判定环与音符重合
        // Phase 3: expired (timeDiff < 0) — 音符淡出

        float approachProgress = 1.0f - (timeDiff / approachMs);
        approachProgress = std::max(0.0f, std::min(1.0f, approachProgress));

        // 音符本体：始终在单元格中心，大小随 approach 增长
        // noteScale: 0.3→1.0，乘以 noteFullW/H 得到实际像素大小
        float noteScale = 0.3f + 0.7f * approachProgress;
        float alpha = 1.0f;

        if (timeDiff <= 0) {
            // 已过判定时间：快速淡出
            float fadeProgress = -timeDiff / 300.0f;
            alpha = 1.0f - std::min(1.0f, fadeProgress);
            noteScale = 1.0f;
        }

        // 判定环（approach circle）：从格子边缘收缩到音符边缘
        // 格子半宽 = gw/2，音符满尺寸半宽 = noteFullW/2
        float cellHalfW = gw * 0.5f;
        float cellHalfH = gh * 0.5f;
        float noteHalfW = noteFullW * 0.5f;
        float noteHalfH = noteFullH * 0.5f;
        // 环当前半径：从格子边缘线性收缩到音符边缘
        float ringHalfW = cellHalfW - (cellHalfW - noteHalfW) * approachProgress;
        float ringHalfH = cellHalfH - (cellHalfH - noteHalfH) * approachProgress;
        float ringAlpha = 0.8f * colDim;
        if (timeDiff <= 0) {
            ringAlpha = 0.0f;  // 过判定时间后环消失
        }

        // ── 绘制判定环 ──
        if (ringAlpha > 0.01f) {
            float ringLineW = 2.5f;  // 线宽
            // 上边
            quads.insert(quads.end(), { cellX - ringHalfW, cellY - ringHalfH, ringHalfW * 2, ringLineW });
            colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, ringAlpha * 0.7f });
            // 下边
            quads.insert(quads.end(), { cellX - ringHalfW, cellY + ringHalfH - ringLineW, ringHalfW * 2, ringLineW });
            colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, ringAlpha * 0.7f });
            // 左边
            quads.insert(quads.end(), { cellX - ringHalfW, cellY - ringHalfH, ringLineW, ringHalfH * 2 });
            colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, ringAlpha * 0.7f });
            // 右边
            quads.insert(quads.end(), { cellX + ringHalfW - ringLineW, cellY - ringHalfH, ringLineW, ringHalfH * 2 });
            colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, ringAlpha * 0.7f });
        }

        // ── 绘制音符本体 ──
        if (note.type == beatmap::NoteType::Tap) {
            float w = noteFullW * noteScale;
            float h = noteFullH * noteScale;
            float x = cellX - w * 0.5f;
            float y = cellY - h * 0.5f;

            quads.insert(quads.end(), { x, y, w, h });

            // 击打窗口内：亮青色 + 高alpha；接近中：暗色；过期：淡出
            float noteAlpha = alpha * 0.9f * colDim;
            if (approachProgress > 0.85f && timeDiff >= 0) {
                // 即将到达判定时间 — 亮色
                float glow = (approachProgress - 0.85f) / 0.15f;  // 0→1
                colors.insert(colors.end(), { 0.0f, 0.7f + 0.3f * glow, 0.68f + 0.28f * glow, noteAlpha });
            } else if (timeDiff < 0) {
                // 过期 — 淡红
                colors.insert(colors.end(), { 0.8f, 0.2f, 0.3f, noteAlpha * 0.5f });
            } else {
                // 接近中 — 暗色
                colors.insert(colors.end(), { 0.0f, 0.5f, 0.48f, noteAlpha * 0.5f });
            }
        } else if (note.type == beatmap::NoteType::Hold) {
            float w = noteFullW * 0.7f;
            float h = noteFullH * 0.7f;
            float holdDuration = static_cast<float>(note.holdEnd - note.time);
            h = std::max(h, std::min(holdDuration * 0.5f, gh * 2.0f));
            float x = cellX - w * 0.5f;
            float y = cellY - h * 0.5f;

            quads.insert(quads.end(), { x, y, w, h });
            float holdAlpha = alpha * 0.7f * colDim;
            if (approachProgress > 0.85f && timeDiff >= 0) {
                colors.insert(colors.end(), { 0.702f, 0.3f, 1.0f, holdAlpha });
            } else {
                colors.insert(colors.end(), { 0.5f, 0.0f, 0.7f, holdAlpha * 0.5f });
            }
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

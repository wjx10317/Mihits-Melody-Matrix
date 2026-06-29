#include "note_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace melody_matrix::renderer {

void NoteRenderer::pushQuad(std::vector<float>& quads, std::vector<float>& colors,
                            std::vector<float>& layers, std::vector<float>& arcSweeps,
                            float x, float y, float w, float h,
                            float r, float g, float b, float a,
                            float layer, float arcSweep) const {
    quads.insert(quads.end(), { x, y, w, h });
    colors.insert(colors.end(), { r, g, b, a });
    layers.push_back(layer);
    arcSweeps.push_back(arcSweep);
}

void NoteRenderer::pushCenteredQuad(std::vector<float>& quads, std::vector<float>& colors,
                                    std::vector<float>& layers, std::vector<float>& arcSweeps,
                                    float cx, float cy, float w, float h,
                                    float r, float g, float b, float a, float layer,
                                    float arcSweep) const {
    pushQuad(quads, colors, layers, arcSweeps,
             cx - w * 0.5f, cy - h * 0.5f, w, h,
             r, g, b, a, layer, arcSweep);
}

float NoteRenderer::holdPushLayerForProgress(float progress) const {
    int stage = std::min(kHoldPushStageCount - 1,
                         static_cast<int>(progress * static_cast<float>(kHoldPushStageCount)));
    if (stage >= 0 && stage < kHoldPushStageCount && m_texHoldPush[static_cast<size_t>(stage)]) {
        return kLayerHoldPushBase + static_cast<float>(stage);
    }
    return -1.0f;
}

bool NoteRenderer::holdPushLayerHasTexture(float layer) const {
    if (layer == kLayerHoldPushRing) return m_texHoldPushRing != nullptr;
    int idx = static_cast<int>(layer - kLayerHoldPushBase + 0.5f);
    if (idx < 0 || idx >= kHoldPushStageCount) return false;
    return m_texHoldPush[static_cast<size_t>(idx)] != nullptr;
}

bool NoteRenderer::init() {
    MM_LOG_INFO("NoteRenderer", "Initializing...");

    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aInstance;
        layout(location = 2) in vec4 aColor;
        layout(location = 3) in float aTexLayer;
        layout(location = 4) in float aArcSweep;

        uniform mat4 uProjection;
        uniform mat4 uModel;
        out vec4 vColor;
        out vec2 vTexCoord;
        out float vTexLayer;
        out float vArcSweep;

        void main() {
            vec2 worldPos = aPos * aInstance.zw + aInstance.xy;
            gl_Position = uProjection * uModel * vec4(worldPos, 0.0, 1.0);
            vColor = aColor;
            vTexCoord = aPos;
            vTexLayer = aTexLayer;
            vArcSweep = aArcSweep;
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        in vec4 vColor;
        in vec2 vTexCoord;
        in float vTexLayer;
        in float vArcSweep;
        out vec4 FragColor;

        uniform sampler2D uTexTap;
        uniform sampler2D uTexSlider;
        uniform sampler2D uTexOverlay;
        uniform sampler2D uTexHoldPushRing;
        uniform sampler2D uTexHoldPush0;
        uniform sampler2D uTexHoldPush1;
        uniform sampler2D uTexHoldPush2;
        uniform sampler2D uTexHoldPush3;
        uniform sampler2D uTexHoldPush4;
        uniform sampler2D uTexHoldPush5;
        uniform sampler2D uTexHoldPush6;
        uniform sampler2D uTexHoldPush7;
        uniform sampler2D uTexHoldPush8;
        uniform sampler2D uTexHoldPush9;
        uniform sampler2D uTexHoldPush10;
        uniform sampler2D uTexBlock;

        void main() {
            int layer = int(vTexLayer + 0.5);
            vec4 texColor = vec4(1.0);

            if (layer == 5)       texColor = texture(uTexBlock, vTexCoord);
            else if (layer == 0)  texColor = texture(uTexTap, vTexCoord);
            else if (layer == 1)  texColor = texture(uTexSlider, vTexCoord);
            else if (layer == 2) {
                // 缩圈/击中：vArcSweep>=0 时 UV 1.35→1.0 收缩；<0 击中扩散
                float uvScale;
                if (vArcSweep < 0.0) {
                    float expand = clamp(-vArcSweep, 0.0, 1.0);
                    uvScale = mix(1.0, 1.28, expand);
                } else {
                    float t = clamp(vArcSweep, 0.0, 1.0);
                    uvScale = mix(1.35, 1.0, t);
                }
                vec2 uv = (vTexCoord - 0.5) / uvScale + 0.5;
                if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
                texColor = texture(uTexOverlay, uv);
            }
            else if (layer == 3)  texColor = texture(uTexHoldPushRing, vTexCoord);
            else if (layer == 6)  texColor = texture(uTexHoldPush0, vTexCoord);
            else if (layer == 7)  texColor = texture(uTexHoldPush1, vTexCoord);
            else if (layer == 8)  texColor = texture(uTexHoldPush2, vTexCoord);
            else if (layer == 9)  texColor = texture(uTexHoldPush3, vTexCoord);
            else if (layer == 10) texColor = texture(uTexHoldPush4, vTexCoord);
            else if (layer == 11) texColor = texture(uTexHoldPush5, vTexCoord);
            else if (layer == 12) texColor = texture(uTexHoldPush6, vTexCoord);
            else if (layer == 13) texColor = texture(uTexHoldPush7, vTexCoord);
            else if (layer == 14) texColor = texture(uTexHoldPush8, vTexCoord);
            else if (layer == 15) texColor = texture(uTexHoldPush9, vTexCoord);
            else if (layer == 16) texColor = texture(uTexHoldPush10, vTexCoord);

            FragColor = texColor * vColor;
        }
    )";

    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        m_shader = std::move(result.value());
    } else {
        MM_LOG_WARN("NoteRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    float quad[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);
    glGenBuffers(1, &m_instanceVbo);
    glGenBuffers(1, &m_colorVbo);
    glGenBuffers(1, &m_layerVbo);
    glGenBuffers(1, &m_arcVbo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(3, 1);

    glBindBuffer(GL_ARRAY_BUFFER, m_arcVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

    m_initialized = true;
    MM_LOG_INFO("NoteRenderer", "Initialized successfully");
    return true;
}

void NoteRenderer::setTextures(const Texture2D* tap, const Texture2D* slider,
                                const Texture2D* overlay,
                                const Texture2D* holdPushRing,
                                const std::array<const Texture2D*, kHoldPushStageCount>& holdPushStages,
                                const Texture2D* block) {
    m_texTap = tap;
    m_texSlider = slider;
    m_texOverlay = overlay;
    m_texHoldPushRing = holdPushRing;
    m_texHoldPush = holdPushStages;
    m_texBlock = block;
}

void NoteRenderer::render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           const std::vector<CellHitEffect>& hitEffects,
                           float scrollOffset, bool scrolling, float scrollProgress,
                           int32_t targetStartCol, int32_t targetEndCol) {
    if (!m_initialized) return;

    std::vector<float> quads;
    std::vector<float> colors;
    std::vector<float> layers;
    std::vector<float> arcSweeps;
    buildNoteVertices(notes, timeMs, rows, cols, ar,
                      activeStartCol, activeEndCol,
                      colHeads, colHeadCount, hitEffects,
                      quads, colors, layers, arcSweeps,
                      scrollOffset, scrolling, scrollProgress,
                      targetStartCol, targetEndCol);

    if (quads.empty()) return;

    int32_t instanceCount = static_cast<int32_t>(quads.size() / 4);
    instanceCount = std::min(instanceCount, m_maxInstances);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, quads.size() * sizeof(float), quads.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, layers.size() * sizeof(float), layers.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_arcVbo);
    glBufferData(GL_ARRAY_BUFFER, arcSweeps.size() * sizeof(float), arcSweeps.data(), GL_DYNAMIC_DRAW);

    if (m_texTap) m_texTap->bind(0);
    if (m_texSlider) m_texSlider->bind(1);
    if (m_texOverlay) m_texOverlay->bind(2);
    if (m_texHoldPushRing) m_texHoldPushRing->bind(3);
    for (int i = 0; i < kHoldPushStageCount; ++i) {
        if (m_texHoldPush[static_cast<size_t>(i)]) {
            m_texHoldPush[static_cast<size_t>(i)]->bind(4 + i);
        }
    }
    if (m_texBlock) m_texBlock->bind(4 + kHoldPushStageCount);

    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    glm::mat4 model = glm::mat4(1.0f);
    if (std::abs(m_animRotation) > 0.001f) {
        glm::vec2 center(1920.0f * 0.5f, 1080.0f * 0.5f);
        model = glm::translate(model, glm::vec3(center, 0.0f));
        model = glm::rotate(model, m_animRotation, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, glm::vec3(-center, 0.0f));
    }
    m_shader.setMat4("uModel", &model[0][0]);

    m_shader.setInt("uTexTap", 0);
    m_shader.setInt("uTexSlider", 1);
    m_shader.setInt("uTexOverlay", 2);
    m_shader.setInt("uTexHoldPushRing", 3);
    m_shader.setInt("uTexHoldPush0", 4);
    m_shader.setInt("uTexHoldPush1", 5);
    m_shader.setInt("uTexHoldPush2", 6);
    m_shader.setInt("uTexHoldPush3", 7);
    m_shader.setInt("uTexHoldPush4", 8);
    m_shader.setInt("uTexHoldPush5", 9);
    m_shader.setInt("uTexHoldPush6", 10);
    m_shader.setInt("uTexHoldPush7", 11);
    m_shader.setInt("uTexHoldPush8", 12);
    m_shader.setInt("uTexHoldPush9", 13);
    m_shader.setInt("uTexHoldPush10", 14);
    m_shader.setInt("uTexBlock", 4 + kHoldPushStageCount);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instanceCount);
    glBindVertexArray(0);

    for (int i = 0; i <= 9; ++i) {
        Texture2D::unbind(i);
    }
}

void NoteRenderer::buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                                      int rows, int cols, float ar,
                                      int32_t activeStartCol, int32_t activeEndCol,
                                      const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                                      const std::vector<CellHitEffect>& hitEffects,
                                      std::vector<float>& quads,
                                      std::vector<float>& colors,
                                      std::vector<float>& layers,
                                      std::vector<float>& arcSweeps,
                                      float scrollOffset, bool scrolling, float scrollProgress,
                                      int32_t targetStartCol, int32_t targetEndCol) {
    (void)scrolling;
    (void)scrollProgress;
    (void)targetStartCol;
    (void)targetEndCol;

    if (rows <= 0 || cols <= 0) return;

    const float W = GridLayout::kScreenW;
    const float H = GridLayout::kScreenH;
    GridLayout layout{ rows, cols, m_blockSize };
    const float gw = layout.gw();
    const float gh = layout.gh();
    const float cellW = layout.contentW();
    const float cellH = layout.contentH();

    float activeLeftX = 0.0f;
    float activeRightX = 0.0f;
    layout.activeBandX(activeStartCol, activeEndCol, activeLeftX, activeRightX);
    const float blockActiveLeftX = activeLeftX;
    const float blockActiveRightX = activeRightX;
    const float contentOverflowX = std::max(0.0f, (cellW - gw) * 0.5f);
    activeLeftX -= contentOverflowX;
    activeRightX += contentOverflowX;

    const float layerTap = m_texTap ? kLayerTap : -1.0f;
    const float layerSlider = m_texSlider ? kLayerSlider : -1.0f;
    const float layerOverlay = m_texOverlay ? kLayerOverlay : -1.0f;
    const float layerBlock = m_texBlock ? kLayerBlock : -1.0f;

    float approachMs = 1800.0f - ar * 120.0f;
    if (approachMs < 300.0f) approachMs = 300.0f;

    std::vector<float> approachQuads;
    std::vector<float> approachColors;
    std::vector<float> approachLayers;
    std::vector<float> approachArcSweeps;

    // ── 格子背景 ──
    if (layerBlock >= 0.0f && rows > 0 && cols > 0) {
        const float blockActiveAlpha = 0.85f;
        const float blockDimAlpha    = 0.35f;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float cellCx = 0.0f;
                float cellCy = 0.0f;
                layout.cellCenter(r, c, activeStartCol, activeEndCol,
                                  scrollOffset, cellCx, cellCy);
                if (!layout.cellVisible(cellCx, cellCy)) continue;

                float slideOffsetX = 0.0f;
                if (m_animSlideRows && r >= m_animPrevRows && m_animPrevRows >= 0) {
                    slideOffsetX = (1.0f - m_animSlideProgress) * (-W);
                }
                float slideOffsetY = 0.0f;
                if (m_animSlideCols && c >= m_animPrevCols && m_animPrevCols >= 0) {
                    slideOffsetY = (1.0f - m_animSlideProgress) * (-H);
                }
                cellCx += slideOffsetX;
                cellCy += slideOffsetY;

                const float blockLeft = cellCx - cellW * 0.5f;
                const float blockRight = cellCx + cellW * 0.5f;
                float overlapLeft  = std::max(blockLeft, blockActiveLeftX);
                float overlapRight = std::min(blockRight, blockActiveRightX);

                auto pushBlock = [&](float x, float w, float alpha) {
                    pushQuad(quads, colors, layers, arcSweeps,
                             x, cellCy - cellH * 0.5f, w, cellH,
                             1.0f, 1.0f, 1.0f, alpha * m_animAlpha * m_globalAlpha,
                             layerBlock);
                };

                if (overlapRight <= overlapLeft) {
                    pushBlock(blockLeft, cellW, blockDimAlpha);
                } else if (overlapLeft <= blockLeft && overlapRight >= blockRight) {
                    pushBlock(blockLeft, cellW, blockActiveAlpha);
                } else {
                    float hlW = overlapRight - overlapLeft;
                    pushBlock(overlapLeft, hlW, blockActiveAlpha);
                    if (blockLeft < overlapLeft) pushBlock(blockLeft, overlapLeft - blockLeft, blockDimAlpha);
                    if (blockRight > overlapRight) pushBlock(overlapRight, blockRight - overlapRight, blockDimAlpha);
                }
            }
        }
    }

    std::array<size_t, 8> colEncounterCount = {};

    for (const auto& note : notes) {
        if (note.col >= 0 && note.col < colHeadCount) {
            size_t& encountered = colEncounterCount[note.col];
            if (encountered < colHeads[note.col]) {
                bool holdingGhost = note.isHold() && (timeMs >= note.time && timeMs <= note.holdEnd);
                if (!holdingGhost) {
                    encountered++;
                    continue;
                }
            } else {
                encountered++;
            }
        }

        float timeDiff = static_cast<float>(note.time - timeMs);
        bool isHolding = note.isHold() && (timeMs >= note.time && timeMs <= note.holdEnd);

        if (!isHolding && (timeDiff > approachMs || timeDiff < -300.0f)) continue;

        float cellX = 0.0f;
        float cellY = 0.0f;
        layout.cellCenter(note.row, note.col, activeStartCol, activeEndCol,
                          scrollOffset, cellX, cellY);
        if (!layout.cellVisible(cellX, cellY)) continue;

        float slideOffsetX = 0.0f;
        if (m_animSlideRows && note.row >= m_animPrevRows && m_animPrevRows >= 0) {
            slideOffsetX = (1.0f - m_animSlideProgress) * (-W);
        }
        float slideOffsetY = 0.0f;
        if (m_animSlideCols && note.col >= m_animPrevCols && m_animPrevCols >= 0) {
            slideOffsetY = (1.0f - m_animSlideProgress) * (-H);
        }
        cellX += slideOffsetX;
        cellY += slideOffsetY;

        float colDim = 1.0f;
        if (cols > 4) {
            float weight = 1.0f;
            if (cellX < activeLeftX) {
                weight = std::max(0.0f, 1.0f - (activeLeftX - cellX) / gw);
            } else if (cellX > activeRightX) {
                weight = std::max(0.0f, 1.0f - (cellX - activeRightX) / gw);
            }
            colDim = 0.25f + 0.75f * weight;
        }

        // approachT: 0=刚进窗口(环贴逻辑格边), 1=判定时刻(环贴 note 边)
        float approachT = 1.0f - (timeDiff / approachMs);
        approachT = std::max(0.0f, std::min(1.0f, approachT));

        float alpha = m_animAlpha * m_globalAlpha * colDim;
        if (timeDiff <= 0 && !isHolding) {
            float fadeProgress = -timeDiff / 300.0f;
            alpha *= 1.0f - std::min(1.0f, fadeProgress);
        }

        // ── 1) Note 本体（与 background 同按 blockSize 缩放）──
        if (note.type == beatmap::NoteType::Tap && layerTap >= 0.0f) {
            float noteAlpha = alpha * 0.95f;
            float r = 0.0f, g = 0.55f, b = 0.52f;
            if (timeDiff <= 0.0f && timeDiff > -150.0f) {
                r = 0.0f; g = 0.95f; b = 0.92f;
            } else if (timeDiff > 0.0f && timeDiff <= approachMs * 0.2f) {
                float t = 1.0f - timeDiff / (approachMs * 0.2f);
                r = 0.0f; g = 0.55f + 0.4f * t; b = 0.52f + 0.4f * t;
            }
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             r, g, b, noteAlpha, layerTap);
        } else if (note.isHold() && layerSlider >= 0.0f) {
            float holdAlpha = alpha * 0.88f;
            float r = 0.5f, g = 0.0f, b = 0.7f;
            if (isHolding || timeDiff <= 0.0f) {
                r = 0.702f; g = 0.3f; b = 1.0f;
            }
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             r, g, b, holdAlpha, layerSlider);
        }

        // ── 2) 缩圈：逻辑格外扩 quad + UV 收缩；延后合批以免被 note 遮挡 ──
        if (layerOverlay >= 0.0f && timeDiff > 0.0f && !isHolding) {
            const bool contentFillsCell = (gw <= cellW + 0.5f);
            const float ringOuterW =
                contentFillsCell ? (cellW * kApproachRingUvOuter) : gw;
            const float ringOuterH =
                contentFillsCell ? (cellH * kApproachRingUvOuter) : gh;
            const float ringW = cellW + (ringOuterW - cellW) * (1.0f - approachT);
            const float ringH = cellH + (ringOuterH - cellH) * (1.0f - approachT);
            // blockSize≈1 时由 quad 外扩承担缩圈；否则 quad 从逻辑格收到 note，UV 继续收缩
            const float ringArcSweep = contentFillsCell ? 1.0f : approachT;
            pushCenteredQuad(approachQuads, approachColors, approachLayers, approachArcSweeps,
                             cellX, cellY, ringW, ringH,
                             1.0f, 1.0f, 1.0f, alpha * 0.95f, layerOverlay,
                             ringArcSweep);
        }

        // ── 3) Hold 按住：holdpush_ring + holdpush_0/10/..../100 铺满整格 ──
        if (isHolding) {
            float holdDuration = static_cast<float>(note.holdEnd - note.time);
            float progress = 0.0f;
            if (holdDuration > 0.0f) {
                progress = static_cast<float>(timeMs - note.time) / holdDuration;
                progress = std::max(0.0f, std::min(1.0f, progress));
            }

            if (m_texHoldPushRing) {
                pushCenteredQuad(quads, colors, layers, arcSweeps,
                                 cellX, cellY, cellW, cellH,
                                 1.0f, 1.0f, 1.0f, alpha * 0.7f, kLayerHoldPushRing);
            }

            float pushLayer = holdPushLayerForProgress(progress);
            if (pushLayer >= 0.0f && holdPushLayerHasTexture(pushLayer)) {
                pushCenteredQuad(quads, colors, layers, arcSweeps,
                                 cellX, cellY, cellW, cellH,
                                 1.0f, 1.0f, 1.0f, alpha, pushLayer);
            }
        }
    }

    // 缩圈置于 note / holdpush 之后绘制，避免被同批实例遮挡
    quads.insert(quads.end(), approachQuads.begin(), approachQuads.end());
    colors.insert(colors.end(), approachColors.begin(), approachColors.end());
    layers.insert(layers.end(), approachLayers.begin(), approachLayers.end());
    arcSweeps.insert(arcSweeps.end(), approachArcSweeps.begin(), approachArcSweeps.end());

    // ── Tap 击中：overlay 从 note 尺寸扩散到格子边 ──
    if (layerOverlay >= 0.0f) {
        for (const auto& hit : hitEffects) {
            if (hit.alpha <= 0.01f) continue;
            float cellX = 0.0f;
            float cellY = 0.0f;
            layout.cellCenter(hit.row, hit.col, activeStartCol, activeEndCol,
                              scrollOffset, cellX, cellY);
            if (!layout.cellVisible(cellX, cellY)) continue;
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             1.0f, 1.0f, 1.0f, hit.alpha * m_globalAlpha, layerOverlay,
                             -(1.0f - hit.alpha));
        }
    }
}

void NoteRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_quadVbo != 0) { glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_instanceVbo != 0) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_colorVbo != 0) { glDeleteBuffers(1, &m_colorVbo); m_colorVbo = 0; }
    if (m_layerVbo != 0) { glDeleteBuffers(1, &m_layerVbo); m_layerVbo = 0; }
    if (m_arcVbo != 0) { glDeleteBuffers(1, &m_arcVbo); m_arcVbo = 0; }
    m_initialized = false;
}

} // namespace melody_matrix::renderer

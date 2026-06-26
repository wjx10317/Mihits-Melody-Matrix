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

    // ── Compile note shader（支持多纹理绑定 + 实例纹理层ID）──
    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;       // unit quad (0,0)-(1,1)
        layout(location = 1) in vec4 aInstance;   // x, y, w, h
        layout(location = 2) in vec4 aColor;      // RGBA tint
        layout(location = 3) in float aTexLayer;  // 纹理层 ID

        uniform mat4 uProjection;
        uniform mat4 uModel;  // 模型变换矩阵（旋转/缩放）
        out vec4 vColor;
        out vec2 vTexCoord;
        out float vTexLayer;

        void main() {
            vec2 worldPos = aPos * aInstance.zw + aInstance.xy;
            gl_Position = uProjection * uModel * vec4(worldPos, 0.0, 1.0);
            vColor = aColor;
            // unit quad 顶点位置直接作为 UV：(0,0)左上 → (1,1)右下
            // 配合 stbi flip_vertically_on_load(1) + Y-down 投影，UV(0,0)对应图像顶部对应屏幕上方
            vTexCoord = aPos;
            vTexLayer = aTexLayer;
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        in vec4 vColor;
        in vec2 vTexCoord;
        in float vTexLayer;
        out vec4 FragColor;

        uniform sampler2D uTexBlock;    // layer 5 (background.png 格子背景块)
        uniform sampler2D uTexTap;      // layer 0
        uniform sampler2D uTexSlider;   // layer 1
        uniform sampler2D uTexOverlay;  // layer 2
        uniform sampler2D uTexSPRing;   // layer 3
        uniform sampler2D uTexSPFull;   // layer 4

        void main() {
            int layer = int(vTexLayer + 0.5);
            vec4 texColor = vec4(1.0);  // 默认白色（纯色模式 layer=-1 或纹理未加载）
            if (layer == 5)      texColor = texture(uTexBlock, vTexCoord);
            else if (layer == 0) texColor = texture(uTexTap, vTexCoord);
            else if (layer == 1) texColor = texture(uTexSlider, vTexCoord);
            else if (layer == 2) texColor = texture(uTexOverlay, vTexCoord);
            else if (layer == 3) texColor = texture(uTexSPRing, vTexCoord);
            else if (layer == 4) texColor = texture(uTexSPFull, vTexCoord);
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

    // ── Create unit quad (two triangles) ──
    float quad[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);
    glGenBuffers(1, &m_instanceVbo);
    glGenBuffers(1, &m_colorVbo);
    glGenBuffers(1, &m_layerVbo);

    glBindVertexArray(m_vao);

    // Quad geometry (location 0) — 同时作为 UV 坐标
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

    // Instance texture layer (location 3): float
    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 1 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 1 * sizeof(float), (void*)0);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);

    m_initialized = true;
    MM_LOG_INFO("NoteRenderer", "Initialized successfully");
    return true;
}

void NoteRenderer::setTextures(const Texture2D* tap, const Texture2D* slider,
                                const Texture2D* overlay, const Texture2D* sliderPushRing,
                                const Texture2D* sliderPushFull, const Texture2D* block) {
    m_texTap = tap;
    m_texSlider = slider;
    m_texOverlay = overlay;
    m_texSliderPushRing = sliderPushRing;
    m_texSliderPushFull = sliderPushFull;
    m_texBlock = block;
}

void NoteRenderer::render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           float scrollOffset, bool scrolling, float scrollProgress,
                           int32_t targetStartCol, int32_t targetEndCol) {
    if (!m_initialized) return;

    std::vector<float> quads;
    std::vector<float> colors;
    std::vector<float> layers;
    buildNoteVertices(notes, timeMs, rows, cols, ar,
                      activeStartCol, activeEndCol,
                      colHeads, colHeadCount, quads, colors, layers,
                      scrollOffset, scrolling, scrollProgress,
                      targetStartCol, targetEndCol);

    if (quads.empty()) return;

    // 注：m_globalAlpha 与 m_animAlpha 已在 buildNoteVertices 中应用到每个颜色 alpha，
    // 这里不再重复乘以 m_globalAlpha（避免双重衰减）。

    int32_t instanceCount = static_cast<int32_t>(quads.size() / 4);
    instanceCount = std::min(instanceCount, m_maxInstances);

    // Update instance buffers
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, quads.size() * sizeof(float), quads.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, layers.size() * sizeof(float), layers.data(), GL_DYNAMIC_DRAW);

    // 绑定多纹理到不同 texture unit
    // 纹理未加载时绑定默认 1x1 白色纹理（或跳过），这里简单处理：仅绑定已加载的
    if (m_texTap) m_texTap->bind(0);
    if (m_texSlider) m_texSlider->bind(1);
    if (m_texOverlay) m_texOverlay->bind(2);
    if (m_texSliderPushRing) m_texSliderPushRing->bind(3);
    if (m_texSliderPushFull) m_texSliderPushFull->bind(4);
    if (m_texBlock) m_texBlock->bind(5);

    // Draw
    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    // 计算模型矩阵（旋转围绕屏幕中心）
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
    m_shader.setInt("uTexSPRing", 3);
    m_shader.setInt("uTexSPFull", 4);
    m_shader.setInt("uTexBlock", 5);

    // 启用 alpha 混合（纹理 PNG 带 alpha 通道）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instanceCount);
    glBindVertexArray(0);

    // 解绑纹理 unit，避免影响后续渲染
    if (m_texTap) Texture2D::unbind(0);
    if (m_texSlider) Texture2D::unbind(1);
    if (m_texOverlay) Texture2D::unbind(2);
    if (m_texSliderPushRing) Texture2D::unbind(3);
    if (m_texSliderPushFull) Texture2D::unbind(4);
    if (m_texBlock) Texture2D::unbind(5);
}

void NoteRenderer::buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                                      int rows, int cols, float ar,
                                      int32_t activeStartCol, int32_t activeEndCol,
                                      const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                                      std::vector<float>& quads,
                                      std::vector<float>& colors,
                                      std::vector<float>& layers,
                                      float scrollOffset, bool scrolling, float scrollProgress,
                                      int32_t targetStartCol, int32_t targetEndCol) {
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / cols;
    const float gh = (H - 2 * margin) / rows;
    // 音符满尺寸 = 格子大小 × m_blockSize（来自 Formation.blockSize，默认 0.9 留 10% 间距）
    const float noteFullW = gw * m_blockSize;
    const float noteFullH = gh * m_blockSize;

    // 动态居中偏移：根据活跃窗口宽度计算（不再硬编码4列的1.5/2.0）
    // cols<KEY_COUNT 时（如3列）用活跃列数算偏移，避免最左列超出屏幕
    float activeWidth = static_cast<float>(activeEndCol - activeStartCol + 1);
    float blockCenterOffset = activeWidth * 0.5f;        // block 左边界居中偏移
    float noteCenterOffset = (activeWidth - 1.0f) * 0.5f; // note 中心居中偏移

    // 纹理层 ID：纹理未加载时用 -1（纯色 fallback）
    const float layerTap      = m_texTap ? 0.0f : -1.0f;
    const float layerSlider   = m_texSlider ? 1.0f : -1.0f;
    const float layerOverlay  = m_texOverlay ? 2.0f : -1.0f;
    const float layerSPRing   = m_texSliderPushRing ? 3.0f : -1.0f;
    const float layerSPFull   = m_texSliderPushFull ? 4.0f : -1.0f;
    const float layerBlock    = m_texBlock ? 5.0f : -1.0f;

    // Approach time based on AR (osu formula)
    float approachMs = 1800.0f - ar * 120.0f;
    if (approachMs < 300.0f) approachMs = 300.0f;

    // ── 渲染判定矩阵的 block 背景（background.png 按块渲染）──
    // 每个格子 (r,c) 渲染一个 background.png quad，256x256 纹理缩放到 gw x gh
    // 位置与网格竖线一致：x = W/2 + (c - activeStartCol - 2)*gw + scrollOffset（格子左边界）
    // 活跃4列正常 alpha，非活跃列半透明（旁边列预览效果）
    if (layerBlock >= 0.0f && rows > 0 && cols > 0) {
        int32_t effStart = scrolling ? targetStartCol : activeStartCol;
        int32_t effEnd   = scrolling ? targetEndCol   : activeEndCol;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float bx = W * 0.5f + (c - activeStartCol - blockCenterOffset) * gw + scrollOffset;
                // 裁剪超出屏幕的列（4列居中时旁边列可能部分超出屏幕边界）
                if (bx + gw < 0.0f || bx > W) continue;
                float by = H - margin - (r + 1) * gh;

                // ── Slide 动画偏移：新行从左滑入，新列从顶部滑下 ──
                float slideOffsetX = 0.0f;
                if (m_animSlideRows && r >= m_animPrevRows && m_animPrevRows >= 0) {
                    slideOffsetX = (1.0f - m_animSlideProgress) * (-W);  // 从左侧滑入
                }
                float slideOffsetY = 0.0f;
                if (m_animSlideCols && c >= m_animPrevCols && m_animPrevCols >= 0) {
                    slideOffsetY = (1.0f - m_animSlideProgress) * (-H);  // 从顶部滑下
                }
                bx += slideOffsetX;
                by += slideOffsetY;

                quads.insert(quads.end(), { bx, by, gw, gh });
                // 活跃列高亮，非活跃列半透明预览
                bool isActive = (c >= effStart && c <= effEnd);
                float blockAlpha = isActive ? 0.85f : 0.35f;
                // 应用矩阵变换动画 alpha 和全局 alpha
                blockAlpha *= m_animAlpha * m_globalAlpha;
                colors.insert(colors.end(), { 1.0f, 1.0f, 1.0f, blockAlpha });
                layers.push_back(layerBlock);
            }
        }
    }

    // colHeads[col] = 该列 JudgeQueue 中已前进的 head 数量
    std::array<size_t, 8> colEncounterCount = {};

    for (const auto& note : notes) {
        // 跳过已判定的音符
        if (note.col >= 0 && note.col < colHeadCount) {
            size_t& encountered = colEncounterCount[note.col];
            if (encountered < colHeads[note.col]) {
                // Hold note 在按住期间不跳过：head 已 advance 但 holdEnd 未到，
                // 需要继续渲染 slider 本体 + 进度光效，体现"按住"视觉
                bool isHolding = (note.type == beatmap::NoteType::Hold) &&
                                 (timeMs >= note.time && timeMs <= note.holdEnd);
                if (!isHolding) {
                    encountered++;
                    continue;
                }
                // 按住期间：不 continue，不 encountered++（避免后续 note 误判为已遇到）
            } else {
                // 未到 head 的 note（encountered >= colHeads）正常计数
                encountered++;
            }
        }

        float timeDiff = static_cast<float>(note.time - timeMs);

        // Hold 按住期间标志（用于保持 alpha 不淡出）
        bool isHolding = (note.type == beatmap::NoteType::Hold) &&
                         (timeMs >= note.time && timeMs <= note.holdEnd);

        // Only render notes within approach window + brief after
        // 按住期间的 Hold 始终渲染（不受 approach 窗口限制）
        if (!isHolding && (timeDiff > approachMs || timeDiff < -300.0f)) continue;

        // 完整矩阵显示 — 4个有效列固定屏幕中央，整体随滚动偏移
        // cellX 公式：以活跃窗口起始列 activeStartCol 为基准，让 col=[startCol, startCol+3] 始终居中
        // 屏幕中央 4 列范围：[W/2 - 2*gw, W/2 + 2*gw]，中心列偏移 -1.5*gw ~ +1.5*gw
        // scrollOffset 在滚动期间为 -colDelta*gw*easedP（向右滚→矩阵左移），完成后归零
        // 滚动期间 activeStartCol 保持旧值，scrollOffset 平滑过渡，完成后 startCol 更新+scrollOffset归零，无跳变
        float cellX = W * 0.5f + (note.col - activeStartCol - noteCenterOffset) * gw + scrollOffset;
        // 裁剪完全超出屏幕的 note（用 note 半宽收紧，避免矩阵变换后新列note出界绘制）
        if (cellX < -noteFullW * 0.5f || cellX > W + noteFullW * 0.5f) continue;
        float cellY = H - margin - (note.row + 0.5f) * gh;

        // ── Slide 动画偏移：新行从左滑入，新列从顶部滑下 ──
        float slideOffsetX = 0.0f;
        if (m_animSlideRows && note.row >= m_animPrevRows && m_animPrevRows >= 0) {
            slideOffsetX = (1.0f - m_animSlideProgress) * (-W);  // 从左侧滑入
        }
        float slideOffsetY = 0.0f;
        if (m_animSlideCols && note.col >= m_animPrevCols && m_animPrevCols >= 0) {
            slideOffsetY = (1.0f - m_animSlideProgress) * (-H);  // 从顶部滑下
        }
        cellX += slideOffsetX;
        cellY += slideOffsetY;

        // 活跃列高亮，非活跃列灰暗
        // 滚动期间使用目标窗口判断活跃列
        float colDim = 1.0f;
        if (cols > 4) {
            int32_t effStart = scrolling ? targetStartCol : activeStartCol;
            int32_t effEnd = scrolling ? targetEndCol : activeEndCol;
            if (note.col < effStart || note.col > effEnd) {
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

        if (timeDiff <= 0 && !isHolding) {
            // 已过判定时间且非按住中：快速淡出
            float fadeProgress = -timeDiff / 300.0f;
            alpha = 1.0f - std::min(1.0f, fadeProgress);
            noteScale = 1.0f;
        } else if (isHolding) {
            // 按住期间：满尺寸、不淡出
            noteScale = 1.0f;
        }

        // 应用矩阵变换动画 alpha 和全局 alpha（影响 note/overlay/sliderpush）
        alpha *= m_animAlpha * m_globalAlpha;

        // 判定环（overlay.png 缩圈）：从格子边缘收缩到音符边缘
        // overlay.png 是矩形框纹理（中间透明），用单个 quad 替代原来的4条线
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
        ringAlpha *= m_animAlpha * m_globalAlpha;

        // ── 绘制判定环（overlay 纹理，单个 quad）──
        if (ringAlpha > 0.01f) {
            float rw = ringHalfW * 2.0f;
            float rh = ringHalfH * 2.0f;
            float rx = cellX - ringHalfW;
            float ry = cellY - ringHalfH;
            quads.insert(quads.end(), { rx, ry, rw, rh });
            // tint 青色 + ringAlpha（纹理本身是白色矩形框，tint 调色）
            colors.insert(colors.end(), { 0.0f, 1.0f, 0.96f, ringAlpha * 0.7f });
            layers.push_back(layerOverlay);
        }

        // ── 绘制音符本体 ──
        if (note.type == beatmap::NoteType::Tap) {
            float w = noteFullW * noteScale;
            float h = noteFullH * noteScale;
            float x = cellX - w * 0.5f;
            float y = cellY - h * 0.5f;

            quads.insert(quads.end(), { x, y, w, h });

            // tint 颜色：击打窗口内亮青色；接近中暗色；过期淡红
            // 纹理 tap.png 是灰度渐变，tint 调色让 note 呈现主题色
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
            layers.push_back(layerTap);
        } else if (note.type == beatmap::NoteType::Hold) {
            // slider 本体（slider.png 纹理）— 大小固定，与 tap 一致用 blockSize 控制，不随 duration 变化
            float w = noteFullW;
            float h = noteFullH;
            float x = cellX - w * 0.5f;
            float y = cellY - h * 0.5f;

            quads.insert(quads.end(), { x, y, w, h });
            float holdAlpha = alpha * 0.7f * colDim;
            if (approachProgress > 0.85f && timeDiff >= 0) {
                colors.insert(colors.end(), { 0.702f, 0.3f, 1.0f, holdAlpha });
            } else {
                colors.insert(colors.end(), { 0.5f, 0.0f, 0.7f, holdAlpha * 0.5f });
            }
            layers.push_back(layerSlider);

            // ── sliderpush 进度光效（hold 进行中：timeMs ∈ [note.time, note.holdEnd]）──
            // 绘制尺寸略大于 slider 本体（×1.125 ≈ 144/128），让光效环绕 note 外围
            // 底图 sliderpush_ring.png 始终显示（alpha 0.5），高亮 sliderpush_100.png alpha 随进度增长
            float holdDuration = static_cast<float>(note.holdEnd - note.time);
            if (timeMs >= note.time && timeMs <= note.holdEnd && holdDuration > 0.0f) {
                float progress = static_cast<float>(timeMs - note.time) / holdDuration;
                progress = std::max(0.0f, std::min(1.0f, progress));

                float spW = w * 1.125f;
                float spH = h * 1.125f;
                float spX = cellX - spW * 0.5f;
                float spY = cellY - spH * 0.5f;

                // 底图 ring
                quads.insert(quads.end(), { spX, spY, spW, spH });
                colors.insert(colors.end(), { 1.0f, 1.0f, 1.0f, 0.5f * colDim * m_animAlpha * m_globalAlpha });
                layers.push_back(layerSPRing);

                // 高亮 full（alpha 随进度从 0 到 1）
                quads.insert(quads.end(), { spX, spY, spW, spH });
                colors.insert(colors.end(), { 1.0f, 1.0f, 1.0f, progress * colDim * m_animAlpha * m_globalAlpha });
                layers.push_back(layerSPFull);
            }
        }
    }
}

void NoteRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_quadVbo != 0) { glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_instanceVbo != 0) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_colorVbo != 0) { glDeleteBuffers(1, &m_colorVbo); m_colorVbo = 0; }
    if (m_layerVbo != 0) { glDeleteBuffers(1, &m_layerVbo); m_layerVbo = 0; }
    m_initialized = false;
}

} // namespace melody_matrix::renderer

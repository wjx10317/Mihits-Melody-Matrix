#include "renderer.h"
#include "note_renderer.h"
#include "renderer/texture_cache.h"
#include "beatmap/note.h"
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
    // Projection is Y-down (top=0, bottom=1080). With stb_flip_on_load=1,
    // OpenGL texture v=0 is image bottom, v=1 is image top.
    // So screen top (Y=0) needs v=1, screen bottom (Y=1080) needs v=0.
    float quadVertices[] = {
        // pos              // texcoord
        0.0f,    0.0f,      0.0f, 1.0f,   // top-left     -> image top-left
        1920.0f, 0.0f,      1.0f, 1.0f,   // top-right    -> image top-right
        1920.0f, 1080.0f,   1.0f, 0.0f,   // bottom-right -> image bottom-right
        0.0f,    0.0f,      0.0f, 1.0f,   // top-left     -> image top-left
        1920.0f, 1080.0f,   1.0f, 0.0f,   // bottom-right -> image bottom-right
        0.0f,    1080.0f,   0.0f, 0.0f,   // bottom-left  -> image bottom-left
    };

    glGenVertexArrays(1, &m_bgVao);
    glGenBuffers(1, &m_bgVbo);

    glBindVertexArray(m_bgVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_bgVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // ── Load default background texture ──
    if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {
        MM_LOG_WARN("Renderer", "Failed to load menu background texture — will use solid color");
    }

    // ── Initialize note renderer ──
    m_noteRenderer = std::make_unique<NoteRenderer>();
    if (!m_noteRenderer->init()) {
        MM_LOG_WARN("Renderer", "NoteRenderer initialization failed");
    }

    // ── 加载 note 纹理资源（从 res/ 目录，相对 exe）──
    // 路径后备：尝试 res/、../res/、../../res/（适配不同工作目录）
    auto loadNoteTexture = [](const std::string& name) -> const Texture2D* {
        auto& cache = TextureCache::instance();
        const std::vector<std::string> prefixes = { "res/", "../res/", "../../res/" };
        for (const auto& prefix : prefixes) {
            std::string path = prefix + name;
            const Texture2D* tex = cache.get(path);
            if (tex && tex->valid()) return tex;
            tex = cache.load(path);
            if (tex && tex->valid()) {
                MM_LOG_INFO("Renderer", "Loaded note texture: %s", path.c_str());
                return tex;
            }
        }
        MM_LOG_WARN("Renderer", "Note texture not found: %s (fallback to solid color)", name.c_str());
        return nullptr;
    };

    const Texture2D* texTap = loadNoteTexture("tap.png");
    const Texture2D* texSlider = loadNoteTexture("slider.png");
    const Texture2D* texOverlay = loadNoteTexture("overlay.png");
    const Texture2D* texSPRing = loadNoteTexture("sliderpush_ring.png");
    const Texture2D* texSPFull = loadNoteTexture("sliderpush_100.png");
    const Texture2D* texBlock = loadNoteTexture("background.png");

    if (m_noteRenderer) {
        m_noteRenderer->setTextures(texTap, texSlider, texOverlay, texSPRing, texSPFull, texBlock);
    }

    m_initialized = true;
    MM_LOG_INFO("Renderer", "Renderer initialized successfully");
    return true;
}

void Renderer::setGameplayRendering(bool enabled) {
    m_gameplayRendering = enabled;
}

void Renderer::setBackgroundPath(const std::string& path) {
    if (path == m_bgPath) return;
    m_bgPath = path;
    m_bgDirty = true;
}

void Renderer::setFormation(int32_t rows, int32_t cols,
                            float blockSize,
                            beatmap::NoteTransformType noteTransformType) {
    m_gridRows = rows;
    m_gridCols = cols;
    m_blockSize = blockSize;
    m_noteTransformType = noteTransformType;
    // 同步 blockSize 到 note renderer
    if (m_noteRenderer) {
        m_noteRenderer->setBlockSize(blockSize);
    }
    // 瞬间切换时清除过渡状态
    m_transition.active = false;
}

void Renderer::beginFormationTransition(int32_t prevRows, int32_t prevCols,
                                          int32_t nextRows, int32_t nextCols) {
    m_transition.prevRows = prevRows;
    m_transition.prevCols = prevCols;
    m_transition.nextRows = nextRows;
    m_transition.nextCols = nextCols;
    m_transition.progress = 0.0f;
    m_transition.active = true;
}

void Renderer::updateFormationTransition(float progress) {
    if (!m_transition.active) return;
    m_transition.progress = std::max(0.0f, std::min(1.0f, progress));
    if (m_transition.progress >= 1.0f) {
        // 过渡完成，切换到新阵型
        m_gridRows = m_transition.nextRows;
        m_gridCols = m_transition.nextCols;
        m_transition.active = false;
    }
}

void Renderer::setNotes(const std::vector<beatmap::Note>& notes, float ar) {
    m_notes = notes;
    m_ar = ar;
}

void Renderer::setScrollState(int32_t activeStartCol, int32_t activeEndCol,
                              int32_t targetStartCol, int32_t targetEndCol,
                              bool scrolling, float scrollProgress) {
    m_activeStartCol = activeStartCol;
    m_activeEndCol = activeEndCol;
    m_targetStartCol = targetStartCol;
    m_targetEndCol = targetEndCol;
    m_scrolling = scrolling;
    m_scrollProgress = scrollProgress;

    // ── 内部统一计算 scrollOffset，确保 renderGrid / renderNotes / note_renderer 三处 gw 基准一致 ──
    // gw 用 m_gridCols（与 renderGrid/note_renderer 完全相同），消除 playing_state 用 currentCols()
    // 与 renderer 用 m_gridCols 在过渡期不同步导致的抽搐和 note 错位。
    // scrollOffset 符号：向右滚(targetStart>activeStart)→矩阵向左移→scrollOffset 为负
    m_scrollOffset = 0.0f;
    if (scrolling && m_gridCols > 0) {
        float p = std::max(0.0f, std::min(1.0f, scrollProgress));
        // ease-in-out 缓动（与原 playing_state 计算一致）
        float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;
        const float W = 1920.0f, margin = 120.0f;
        float gw = (W - 2 * margin) / m_gridCols;
        int32_t colDelta = targetStartCol - activeStartCol;
        m_scrollOffset = -static_cast<float>(colDelta) * gw * easedP;
    }
}

void Renderer::setColumnHeads(const std::array<size_t, 8>& heads, int32_t columnCount) {
    m_colHeads = heads;
    m_colHeadCount = columnCount;
}

void Renderer::setBgDim(float dim) {
    m_bgDim = std::max(0.0f, std::min(1.0f, dim));
}

void Renderer::renderFrame(int64_t interpolatedTimeMs) {
    if (!m_initialized) return;

    // 延迟加载背景纹理
    if (m_bgDirty) {
        m_bgDirty = false;
        if (m_bgPath.empty()) {
            // 恢复默认背景
            m_bgTexture = Texture2D();
            if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {
                MM_LOG_WARN("Renderer", "Failed to reload default background");
            }
        } else {
            // 优先尝试.mp4（暂不支持视频，跳过）
            // 其次加载图片
            Texture2D newTex;
            if (!newTex.loadFromFile(m_bgPath)) {
                MM_LOG_WARN("Renderer", "Failed to load background: " + m_bgPath);
                // 加载失败则用默认
                m_bgTexture = Texture2D();
                if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {
                    MM_LOG_WARN("Renderer", "Failed to reload default background");
                }
            } else {
                m_bgTexture = std::move(newTex);
                MM_LOG_INFO("Renderer", "Background loaded: " + m_bgPath);
            }
        }
    }

    renderBackground();
    if (m_gameplayRendering) {
        renderGrid(interpolatedTimeMs);
        renderNotes(interpolatedTimeMs);
    }
}

void Renderer::renderBackground() {
    if (!m_bgTexture.valid() || !m_bgShader.valid()) {
        return;
    }

    m_bgTexture.bind(0);
    m_bgShader.use();

    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_bgShader.setMat4("uProjection", &proj[0][0]);
    m_bgShader.setInt("uTexture", 0);
    m_bgShader.setFloat("uAlpha", 1.0f);

    glBindVertexArray(m_bgVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    Texture2D::unbind(0);

    // ── 背景遮罩层 ──
    if (m_bgDim > 0.001f) {
        m_gridShader.use();
        m_gridShader.setMat4("uProjection", &proj[0][0]);
        m_gridShader.setVec4("uColor", 0.063f, 0.063f, 0.118f, m_bgDim);

        // 复用背景四边形的 VAO 绘制全屏遮罩
        // 但背景 VAO 有两个属性（pos + texcoord），grid shader 只用 location=0
        // 所以需要用 grid VAO 构建一个全屏四边形
        float dimQuad[] = {
            0.0f,    0.0f,
            1920.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,    0.0f,
            1920.0f, 1080.0f,
            0.0f,    1080.0f,
        };
        glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(dimQuad), dimQuad, GL_DYNAMIC_DRAW);
        glBindVertexArray(m_gridVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
}

void Renderer::renderGrid(int64_t /*timeMs*/) {
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;

    if (m_transition.active) {
        // ── Fade 过渡渲染：旧网格淡出 + 新网格淡入 ──
        float p = m_transition.progress;
        float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;

        // 旧网格（淡出）
        {
            float gw = (W - 2 * margin) / m_transition.prevCols;
            float gh = (H - 2 * margin) / m_transition.prevRows;
            std::vector<float> lines;
            for (int c = 0; c <= m_transition.prevCols; ++c) {
                float x = margin + c * gw;
                lines.push_back(x); lines.push_back(margin);
                lines.push_back(x); lines.push_back(H - margin);
            }
            for (int r = 0; r <= m_transition.prevRows; ++r) {
                float y = margin + r * gh;
                lines.push_back(margin); lines.push_back(y);
                lines.push_back(W - margin); lines.push_back(y);
            }
            int32_t vc = static_cast<int32_t>(lines.size() / 2);
            glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
            glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);
            m_gridShader.use();
            glm::mat4 proj = glm::ortho(0.0f, W, H, 0.0f, -1.0f, 1.0f);
            m_gridShader.setMat4("uProjection", &proj[0][0]);
            float oldAlpha = 0.3f * (1.0f - easedP);
            m_gridShader.setVec4("uColor", 0.0f, 1.0f, 0.96f, oldAlpha);
            glBindVertexArray(m_gridVao);
            glDrawArrays(GL_LINES, 0, vc);
            glBindVertexArray(0);
        }

        // 新网格（淡入）
        {
            float gw = (W - 2 * margin) / m_transition.nextCols;
            float gh = (H - 2 * margin) / m_transition.nextRows;
            std::vector<float> lines;
            for (int c = 0; c <= m_transition.nextCols; ++c) {
                float x = margin + c * gw;
                lines.push_back(x); lines.push_back(margin);
                lines.push_back(x); lines.push_back(H - margin);
            }
            for (int r = 0; r <= m_transition.nextRows; ++r) {
                float y = margin + r * gh;
                lines.push_back(margin); lines.push_back(y);
                lines.push_back(W - margin); lines.push_back(y);
            }
            int32_t vc = static_cast<int32_t>(lines.size() / 2);
            glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
            glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);
            m_gridShader.use();
            glm::mat4 proj = glm::ortho(0.0f, W, H, 0.0f, -1.0f, 1.0f);
            m_gridShader.setMat4("uProjection", &proj[0][0]);
            float newAlpha = 0.3f * easedP;
            m_gridShader.setVec4("uColor", 0.0f, 1.0f, 0.96f, newAlpha);
            glBindVertexArray(m_gridVao);
            glDrawArrays(GL_LINES, 0, vc);
            glBindVertexArray(0);
        }
    } else {
        // ── 正常渲染：4列活跃窗口固定屏幕中央，整体随滚动偏移 ──
        const float gw = (W - 2 * margin) / m_gridCols;
        const float gh = (H - 2 * margin) / m_gridRows;
        // 活跃4列在屏幕中央的范围 [W/2-2*gw, W/2+2*gw]（不随滚动变化）
        const float activeLeft = W * 0.5f - 2.0f * gw;
        const float activeRight = W * 0.5f + 2.0f * gw;
        // 滚动期间用旧 activeStartCol 作基准，scrollOffset 平滑过渡
        int32_t baseStart = m_activeStartCol;

        std::vector<float> lines;

        // 竖线 — 所有列都显示，整体随 scrollOffset 偏移，让活跃4列居中
        for (int c = 0; c <= m_gridCols; ++c) {
            float x = W * 0.5f + (c - baseStart - 2) * gw + m_scrollOffset;
            lines.push_back(x); lines.push_back(margin);
            lines.push_back(x); lines.push_back(H - margin);
        }

        // 横线 — 仅在活跃4列范围内绘制（固定屏幕中央，不随滚动偏移）
        for (int r = 0; r <= m_gridRows; ++r) {
            float y = margin + r * gh;
            lines.push_back(activeLeft); lines.push_back(y);
            lines.push_back(activeRight); lines.push_back(y);
        }

        int32_t vertexCount = static_cast<int32_t>(lines.size() / 2);

        glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
        glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);

        m_gridShader.use();

        glm::mat4 proj = glm::ortho(0.0f, W, H, 0.0f, -1.0f, 1.0f);
        m_gridShader.setMat4("uProjection", &proj[0][0]);

        // 活跃列高亮，非活跃列灰暗
        m_gridShader.setVec4("uColor", 0.0f, 1.0f, 0.96f, 0.15f);
        glBindVertexArray(m_gridVao);
        glDrawArrays(GL_LINES, 0, vertexCount);
        glBindVertexArray(0);

        // 活跃列竖线高亮（应用滚动偏移）
        if (m_gridCols > 4) {
            std::vector<float> activeLines;
            int32_t activeStart = m_scrolling ? m_targetStartCol : m_activeStartCol;
            int32_t activeEnd = m_scrolling ? m_targetEndCol : m_activeEndCol;
            for (int c = activeStart; c <= activeEnd + 1 && c <= m_gridCols; ++c) {
                float x = W * 0.5f + (c - baseStart - 2) * gw + m_scrollOffset;
                activeLines.push_back(x); activeLines.push_back(margin);
                activeLines.push_back(x); activeLines.push_back(H - margin);
            }
            if (!activeLines.empty()) {
                int32_t activeVC = static_cast<int32_t>(activeLines.size() / 2);
                glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
                glBufferData(GL_ARRAY_BUFFER, activeLines.size() * sizeof(float), activeLines.data(), GL_DYNAMIC_DRAW);
                m_gridShader.setVec4("uColor", 0.0f, 1.0f, 0.96f, 0.4f);
                glBindVertexArray(m_gridVao);
                glDrawArrays(GL_LINES, 0, activeVC);
                glBindVertexArray(0);
            }
        }
    }
}

void Renderer::renderNotes(int64_t timeMs) {
    if (m_notes.empty() || !m_noteRenderer) return;

    if (m_transition.active) {
        float p = m_transition.progress;
        int32_t rows = p < 0.5f ? m_transition.prevRows : m_transition.nextRows;
        int32_t cols = p < 0.5f ? m_transition.prevCols : m_transition.nextCols;
        m_noteRenderer->render(m_notes, timeMs, rows, cols, m_ar,
                               m_activeStartCol, m_activeEndCol,
                               m_colHeads, m_colHeadCount,
                               m_scrollOffset, m_scrolling, m_scrollProgress,
                               m_targetStartCol, m_targetEndCol);
    } else {
        m_noteRenderer->render(m_notes, timeMs, m_gridRows, m_gridCols, m_ar,
                               m_activeStartCol, m_activeEndCol,
                               m_colHeads, m_colHeadCount,
                               m_scrollOffset, m_scrolling, m_scrollProgress,
                               m_targetStartCol, m_targetEndCol);
    }
}

void Renderer::shutdown() {
    m_noteRenderer.reset();

    m_bgTexture = Texture2D();
    if (m_bgVao != 0) {
        glDeleteVertexArrays(1, &m_bgVao);
        m_bgVao = 0;
    }
    if (m_bgVbo != 0) {
        glDeleteBuffers(1, &m_bgVbo);
        m_bgVbo = 0;
    }

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

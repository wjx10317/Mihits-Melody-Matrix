// ============================================================
// note_renderer.cpp — 音符实例化渲染实现
//
// OpenGL 实例化架构：
//   - 静态 VBO：单位四边形 UV (0,0)-(1,1)，6 顶点
//   - 实例 VBO（divisor=1）：位置尺寸 / 颜色 / 层 ID / arcSweep
//   - 一次 glDrawArraysInstanced 绘制全部实例（画家算法靠 CPU 插入顺序）
//
// buildNoteVertices 绘制顺序（从先到后 = 从底到顶）：
//   1. 格子 background（layer 5）— 全矩阵遍历
//   2. 各 note 本体（layer 0 tap / layer 1 slider）
//   3. 各 note hold 按住（layer 3 ring + layer 6..16 push）
//   4. 缩圈 approach ring（layer 2）— 延迟合批，避免被 note 遮挡
//   5. Tap 击中扩散（layer 2, arcSweep<0）— 最顶层
// ============================================================

#include "note_renderer.h"   // 本模块头文件
#include "util/logger.h"     // MM_LOG_INFO / MM_LOG_WARN 日志宏

#include <glad.h>                          // OpenGL 函数加载
#include <glm/glm.hpp>                     // glm 向量/矩阵基础类型
#include <glm/gtc/matrix_transform.hpp>    // ortho / translate / rotate 等
#include <algorithm>                       // std::min / std::max
#include <array>                           // std::array
#include <cmath>                           // std::abs

namespace melody_matrix::renderer {

// ── 实例数据追加辅助 ──

/// 向实例缓冲追加一个轴对齐四边形（左上角 xy + 宽高 wh）
void NoteRenderer::pushQuad(std::vector<float>& quads, std::vector<float>& colors,
                            std::vector<float>& layers, std::vector<float>& arcSweeps,
                            float x, float y, float w, float h,
                            float r, float g, float b, float a,
                            float layer, float arcSweep) const {
    quads.insert(quads.end(), { x, y, w, h });           // 实例位置尺寸 vec4
    colors.insert(colors.end(), { r, g, b, a });         // 实例 tint 颜色 RGBA
    layers.push_back(layer);                             // 纹理层 ID（fragment 分支用）
    arcSweeps.push_back(arcSweep);                       // 缩圈/击中 UV 参数
}

/// 以中心点 (cx,cy) 计算左上角后调用 pushQuad
void NoteRenderer::pushCenteredQuad(std::vector<float>& quads, std::vector<float>& colors,
                                    std::vector<float>& layers, std::vector<float>& arcSweeps,
                                    float cx, float cy, float w, float h,
                                    float r, float g, float b, float a, float layer,
                                    float arcSweep) const {
    pushQuad(quads, colors, layers, arcSweeps,
             cx - w * 0.5f, cy - h * 0.5f, w, h,  // 中心转左上角
             r, g, b, a, layer, arcSweep);
}

/// Hold 按住进度 → holdpush 阶段纹理层（kLayerHoldPushBase + stage）
float NoteRenderer::holdPushLayerForProgress(float progress) const {
    // 将 [0,1] 进度映射到 0..kHoldPushStageCount-1 阶段索引
    int stage = std::min(kHoldPushStageCount - 1,
                         static_cast<int>(progress * static_cast<float>(kHoldPushStageCount)));
    // 该阶段纹理存在则返回对应层 ID，否则返回 -1 表示跳过
    if (stage >= 0 && stage < kHoldPushStageCount && m_texHoldPush[static_cast<size_t>(stage)]) {
        return kLayerHoldPushBase + static_cast<float>(stage);
    }
    return -1.0f;
}

/// 检查指定纹理层是否有已注入的有效纹理指针
bool NoteRenderer::holdPushLayerHasTexture(float layer) const {
    if (layer == kLayerHoldPushRing) return m_texHoldPushRing != nullptr;  // 外环层 3
    // holdpush 层 6..16 → 数组下标 0..10
    int idx = static_cast<int>(layer - kLayerHoldPushBase + 0.5f);
    if (idx < 0 || idx >= kHoldPushStageCount) return false;
    return m_texHoldPush[static_cast<size_t>(idx)] != nullptr;
}

// ── 初始化：编译 shader + 配置实例化 VAO ──

bool NoteRenderer::init() {
    MM_LOG_INFO("NoteRenderer", "Initializing...");

    // ══════════════════════════════════════════════════════════
    // 【顶点着色器 vertSrc】
    // 职责：将共享单位四边形 aPos 与每实例 aInstance(xywh) 组合为世界坐标，
    //       并把颜色、UV、纹理层、arcSweep 传给片段着色器。
    // ══════════════════════════════════════════════════════════
    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;          // 单位四边形局部坐标，兼作 UV
        layout(location = 1) in vec4 aInstance;       // 实例：xy=左上角, zw=宽高
        layout(location = 2) in vec4 aColor;        // 实例 tint RGBA
        layout(location = 3) in float aTexLayer;    // 实例纹理层 ID
        layout(location = 4) in float aArcSweep;     // 缩圈/击中 UV 缩放参数

        uniform mat4 uProjection;   // 正交投影（屏幕像素空间）
        uniform mat4 uModel;        // 阵型旋转等模型变换
        out vec4 vColor;            // 传给 fragment 的颜色
        out vec2 vTexCoord;         // 传给 fragment 的 UV
        out float vTexLayer;        // 传给 fragment 的层 ID
        out float vArcSweep;        // 传给 fragment 的 arcSweep

        void main() {
            // 局部 (0,1) 缩放到实例宽高并平移到左上角 → 世界像素坐标
            vec2 worldPos = aPos * aInstance.zw + aInstance.xy;
            gl_Position = uProjection * uModel * vec4(worldPos, 0.0, 1.0);
            vColor = aColor;
            vTexCoord = aPos;       // UV 与单位四边形顶点一致
            vTexLayer = aTexLayer;
            vArcSweep = aArcSweep;
        }
    )";

    // ══════════════════════════════════════════════════════════
    // 【片段着色器 fragSrc】
    // 职责：按 vTexLayer 整数分支采样对应 sampler2D；
    //       layer 2 特殊：vArcSweep>=0 缩圈（UV 1.35→1.0），<0 击中扩散（1.0→1.28）。
    //
    // ── CPU 层 ID → fragment 分支 → OpenGL 纹理单元 完整映射表 ──
    //   层 ID | 含义              | uniform           | bind 单元
    //   ------|-------------------|-------------------|----------
    //     5   | 格子 background   | uTexBlock         | 15
    //     0   | Tap note          | uTexTap           | 0
    //     1   | Hold note 本体    | uTexSlider        | 1
    //     2   | 缩圈 / 击中 overlay | uTexOverlay     | 2
    //     3   | Hold 按住外环     | uTexHoldPushRing  | 3
    //     6   | holdpush_0%       | uTexHoldPush0     | 4
    //     7   | holdpush_10%      | uTexHoldPush1     | 5
    //     8   | holdpush_20%      | uTexHoldPush2     | 6
    //     9   | holdpush_30%      | uTexHoldPush3     | 7
    //    10   | holdpush_40%      | uTexHoldPush4     | 8
    //    11   | holdpush_50%      | uTexHoldPush5     | 9
    //    12   | holdpush_60%      | uTexHoldPush6     | 10
    //    13   | holdpush_70%      | uTexHoldPush7     | 11
    //    14   | holdpush_80%      | uTexHoldPush8     | 12
    //    15   | holdpush_90%      | uTexHoldPush9     | 13
    //    16   | holdpush_100%     | uTexHoldPush10    | 14
    //
    // layer 2 特殊：vArcSweep >= 0 缩圈（UV 1.35→1.0）；< 0 击中扩散（UV 1.0→1.28）
    // ══════════════════════════════════════════════════════════
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
            int layer = int(vTexLayer + 0.5);   // 浮点层 ID 四舍五入为整数
            vec4 texColor = vec4(1.0);          // 默认白色（无分支时）

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

            FragColor = texColor * vColor;   // 纹理 × 实例 tint
        }
    )";

    // ── 编译 vert + frag，失败则记录警告并返回 false ──
    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        m_shader = std::move(result.value());
    } else {
        MM_LOG_WARN("NoteRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    // ══════════════════════════════════════════════════════════
    // 【VAO / VBO 设置】
    // 静态 quadVbo：6 顶点单位四边形；4 个动态实例 VBO（divisor=1）。
    // ══════════════════════════════════════════════════════════

    // 共享单位四边形：2 三角 6 顶点，aPos 同时作为 UV (0,0)-(1,1)
    float quad[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,   // 三角 1：左下、右下、右上
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f    // 三角 2：左下、右上、左上
    };

    // 生成 VAO 与 5 个 VBO 句柄
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);
    glGenBuffers(1, &m_instanceVbo);
    glGenBuffers(1, &m_colorVbo);
    glGenBuffers(1, &m_layerVbo);
    glGenBuffers(1, &m_arcVbo);

    glBindVertexArray(m_vao);  // 后续顶点属性配置绑定到此 VAO

    // ── location 0：静态四边形顶点（每实例复用，divisor 默认 0）──
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // ── location 1：实例属性 xywh（divisor=1 → 每实例前进一步）──
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(1, 1);

    // ── location 2：实例颜色 RGBA ──
    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    // ── location 3：实例纹理层 ID ──
    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(3, 1);

    // ── location 4：缩圈/击中 UV 参数 arcSweep ──
    glBindBuffer(GL_ARRAY_BUFFER, m_arcVbo);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);  // 解绑 VAO，避免误改配置

    m_quads.reserve(static_cast<size_t>(m_maxInstances) * 4);
    m_colors.reserve(static_cast<size_t>(m_maxInstances) * 4);
    m_layers.reserve(static_cast<size_t>(m_maxInstances));
    m_arcSweeps.reserve(static_cast<size_t>(m_maxInstances));

    m_initialized = true;
    MM_LOG_INFO("NoteRenderer", "Initialized successfully");
    return true;
}

/// 注入各层纹理指针，render 时按层 ID 绑定到对应纹理单元
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

// ── 每帧渲染：构建实例 → 上传 VBO → 绑定纹理 → 实例化绘制 ──

void NoteRenderer::render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           const std::vector<CellHitEffect>& hitEffects,
                           float scrollOffset, bool scrolling, float scrollProgress,
                           int32_t targetStartCol, int32_t targetEndCol) {
    if (!m_initialized) return;  // 未 init 则直接返回

    m_quads.clear();
    m_colors.clear();
    m_layers.clear();
    m_arcSweeps.clear();

    buildNoteVertices(notes, timeMs, rows, cols, ar,
                      activeStartCol, activeEndCol,
                      colHeads, colHeadCount, hitEffects,
                      m_quads, m_colors, m_layers, m_arcSweeps,
                      scrollOffset, scrolling, scrollProgress,
                      targetStartCol, targetEndCol);

    if (m_quads.empty()) return;

    int32_t instanceCount = static_cast<int32_t>(m_quads.size() / 4);
    instanceCount = std::min(instanceCount, m_maxInstances);

    const size_t quadFloats = static_cast<size_t>(instanceCount) * 4;
    const size_t colorFloats = quadFloats;
    const size_t layerFloats = static_cast<size_t>(instanceCount);
    const size_t arcFloats = static_cast<size_t>(instanceCount);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, quadFloats * sizeof(float), m_quads.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_colorVbo);
    glBufferData(GL_ARRAY_BUFFER, colorFloats * sizeof(float), m_colors.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_layerVbo);
    glBufferData(GL_ARRAY_BUFFER, layerFloats * sizeof(float), m_layers.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_arcVbo);
    glBufferData(GL_ARRAY_BUFFER, arcFloats * sizeof(float), m_arcSweeps.data(), GL_DYNAMIC_DRAW);

    // ══════════════════════════════════════════════════════════
    // 【纹理绑定】各层纹理绑定到与 fragment uniform 一致的纹理单元
    // ══════════════════════════════════════════════════════════
    if (m_texTap) m_texTap->bind(0);
    if (m_texSlider) m_texSlider->bind(1);
    if (m_texOverlay) m_texOverlay->bind(2);
    if (m_texHoldPushRing) m_texHoldPushRing->bind(3);
    for (int i = 0; i < kHoldPushStageCount; ++i) {
        if (m_texHoldPush[static_cast<size_t>(i)]) {
            m_texHoldPush[static_cast<size_t>(i)]->bind(4 + i);  // holdpush 单元 4..14
        }
    }
    if (m_texBlock) m_texBlock->bind(4 + kHoldPushStageCount);  // block 单元 15

    // 激活 shader 并设置投影矩阵（1920×1080 正交，Y 轴向下）
    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    // 阵型旋转过渡：绕屏幕中心 (960,540) 应用 uModel
    glm::mat4 model = glm::mat4(1.0f);
    if (std::abs(m_animRotation) > 0.001f) {
        glm::vec2 center(1920.0f * 0.5f, 1080.0f * 0.5f);
        model = glm::translate(model, glm::vec3(center, 0.0f));
        model = glm::rotate(model, m_animRotation, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, glm::vec3(-center, 0.0f));
    }
    m_shader.setMat4("uModel", &model[0][0]);

    // 告知 fragment shader 各 sampler 对应的纹理单元索引
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

    // ══════════════════════════════════════════════════════════
    // 【绘制】Alpha 混合 + 一次实例化 draw call
    // ══════════════════════════════════════════════════════════
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instanceCount);  // 6 顶点/实例 × N 实例
    glBindVertexArray(0);

    // 解绑纹理单元 0..9，避免污染后续渲染
    for (int i = 0; i <= 9; ++i) {
        Texture2D::unbind(i);
    }
}

// ── buildNoteVertices：CPU 端构建全部实例，控制 Z 序（插入顺序）──

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
    (void)scrolling;        // 预留参数，当前未使用
    (void)scrollProgress;
    (void)targetStartCol;
    (void)targetEndCol;

    if (rows <= 0 || cols <= 0) return;  // 无效矩阵尺寸

    // ── 布局与尺寸：逻辑格 → 屏幕像素；blockSize 缩放格内内容 ──
    const float W = GridLayout::kScreenW;   // 屏幕宽 1920
    const float H = GridLayout::kScreenH;   // 屏幕高 1080
    GridLayout layout{ rows, cols, m_blockSize };
    const float gw = layout.gw();           // 逻辑格宽（未 block 缩放）
    const float gh = layout.gh();           // 逻辑格高
    const float cellW = layout.contentW();  // gw × blockSize，格内绘制宽
    const float cellH = layout.contentH();  // gh × blockSize，格内绘制高

    // 活跃列带 X 范围（background 高亮/半透明分割用）
    float activeLeftX = 0.0f;
    float activeRightX = 0.0f;
    layout.activeBandX(activeStartCol, activeEndCol, activeLeftX, activeRightX);
    const float blockActiveLeftX = activeLeftX;    // 逻辑格边界的活跃左界
    const float blockActiveRightX = activeRightX;  // 逻辑格边界的活跃右界
    const float contentOverflowX = std::max(0.0f, (cellW - gw) * 0.5f);  // block 外扩超出逻辑格
    activeLeftX -= contentOverflowX;   // 扩展到 block 绘制边界
    activeRightX += contentOverflowX;

    // 纹理层有效性：无纹理则层 ID 设为 -1，后续跳过
    const float layerTap = m_texTap ? kLayerTap : -1.0f;
    const float layerSlider = m_texSlider ? kLayerSlider : -1.0f;
    const float layerOverlay = m_texOverlay ? kLayerOverlay : -1.0f;
    const float layerBlock = m_texBlock ? kLayerBlock : -1.0f;

    // AR 决定缩圈提前量：AR 越高窗口越短
    float approachMs = 1800.0f - ar * 120.0f;
    if (approachMs < 300.0f) approachMs = 300.0f;

    // 缩圈单独缓冲，最后 append 以保证绘制在 note 之上
    std::vector<float> approachQuads;
    std::vector<float> approachColors;
    std::vector<float> approachLayers;
    std::vector<float> approachArcSweeps;

    // ══════════════════════════════════════════════════════════
    // 【阶段 1：格子 background（layer 5）— 最底层】
    // 全矩阵遍历；活跃列带内 alpha=0.85，带外 alpha=0.35；
    // 与 note 同用 blockSize 缩放；可按活跃带水平分割多段 quad。
    // ══════════════════════════════════════════════════════════
    if (layerBlock >= 0.0f && rows > 0 && cols > 0) {
        const float blockActiveAlpha = 0.85f;  // 活跃列带内 background 透明度
        const float blockDimAlpha    = 0.35f;  // 带外 dim 透明度

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float cellCx = 0.0f;
                float cellCy = 0.0f;
                layout.cellCenter(r, c, activeStartCol, activeEndCol,
                                  scrollOffset, cellCx, cellCy);
                if (!layout.cellVisible(cellCx, cellCy)) continue;  // 屏外格跳过

                // 阵型滑入动画：新行/列从屏外滑入
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

                // 局部 lambda：在指定 x,w 处推送一段 background quad
                auto pushBlock = [&](float x, float w, float alpha) {
                    pushQuad(quads, colors, layers, arcSweeps,
                             x, cellCy - cellH * 0.5f, w, cellH,
                             1.0f, 1.0f, 1.0f, alpha * m_animAlpha * m_globalAlpha,
                             layerBlock);
                };

                // 按与活跃列带重叠情况分割 alpha（全 dim / 全亮 / 左中右三段）
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

    std::array<size_t, 8> colEncounterCount = {};  // 每列已 encounter 的 note 计数（配合 colHeads）

    // ══════════════════════════════════════════════════════════
    // 【阶段 2~3：遍历 note — 本体 + hold 按住】
    // 写入主 quads 缓冲；缩圈暂存 approachQuads，阶段 4 再合批。
    // ══════════════════════════════════════════════════════════
    for (const auto& note : notes) {
        // 跳过已判定 note（colHeads 列头指针）；hold 按住中保留 ghost
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

        float timeDiff = static_cast<float>(note.time - timeMs);  // 正=未到，负=已过
        bool isHolding = note.isHold() && (timeMs >= note.time && timeMs <= note.holdEnd);

        // 可见窗口：approach 提前量内，或 hold 中；已过 300ms 则隐藏
        if (!isHolding && (timeDiff > approachMs || timeDiff < -300.0f)) continue;

        float cellX = 0.0f;
        float cellY = 0.0f;
        layout.cellCenter(note.row, note.col, activeStartCol, activeEndCol,
                          scrollOffset, cellX, cellY);
        if (!layout.cellVisible(cellX, cellY)) continue;

        // 阵型滑入偏移（与 background 一致）
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

        // 宽矩阵（>4 列）时非活跃列渐暗
        float colDim = 1.0f;
        if (cols > 4) {
            float weight = 1.0f;
            if (cellX < activeLeftX) {
                weight = std::max(0.0f, 1.0f - (activeLeftX - cellX) / gw);
            } else if (cellX > activeRightX) {
                weight = std::max(0.0f, 1.0f - (cellX - activeRightX) / gw);
            }
            colDim = 0.25f + 0.75f * weight;  // 最暗 25%，活跃带内 100%
        }

        // approachT: 0=刚进窗口(环贴逻辑格边), 1=判定时刻(环贴 note 边)
        float approachT = 1.0f - (timeDiff / approachMs);
        approachT = std::max(0.0f, std::min(1.0f, approachT));

        float alpha = m_animAlpha * m_globalAlpha * colDim;
        if (timeDiff <= 0 && !isHolding) {
            float fadeProgress = -timeDiff / 300.0f;  // 击中后 300ms 淡出
            alpha *= 1.0f - std::min(1.0f, fadeProgress);
        }

        // ── 2a) Note 本体（layer 0 tap / layer 1 slider，尺寸 cellW×cellH）──
        if (note.type == beatmap::NoteType::Tap && layerTap >= 0.0f) {
            float noteAlpha = alpha * 0.95f;
            float r = 0.0f, g = 0.55f, b = 0.52f;  // 默认青绿 tint
            if (timeDiff <= 0.0f && timeDiff > -150.0f) {
                r = 0.0f; g = 0.95f; b = 0.92f;  // 击中后高亮
            } else if (timeDiff > 0.0f && timeDiff <= approachMs * 0.2f) {
                float t = 1.0f - timeDiff / (approachMs * 0.2f);
                r = 0.0f; g = 0.55f + 0.4f * t; b = 0.52f + 0.4f * t;  // 临近判定渐亮
            }
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             r, g, b, noteAlpha, layerTap);
        } else if (note.isHold() && layerSlider >= 0.0f) {
            float holdAlpha = alpha * 0.88f;
            float r = 0.5f, g = 0.0f, b = 0.7f;  // 默认紫 tint
            if (isHolding || timeDiff <= 0.0f) {
                r = 0.702f; g = 0.3f; b = 1.0f;  // 按住/已击中高亮
            }
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             r, g, b, holdAlpha, layerSlider);
        }

        // ── 2b) 缩圈（layer 2）— 写入 approachQuads，阶段 4 合批 ──
        if (layerOverlay >= 0.0f && timeDiff > 0.0f && !isHolding) {
            const bool contentFillsCell = (gw <= cellW + 0.5f);  // blockSize≈1 时内容填满逻辑格
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

        // ── 2c) Hold 按住：ring（layer 3）+ push 阶段（layer 6..16）──
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

    // ══════════════════════════════════════════════════════════
    // 【阶段 4：缩圈合批】
    // 将 approachQuads 追加到主缓冲末尾，绘制在 note/holdpush 之上。
    // ══════════════════════════════════════════════════════════
    quads.insert(quads.end(), approachQuads.begin(), approachQuads.end());
    colors.insert(colors.end(), approachColors.begin(), approachColors.end());
    layers.insert(layers.end(), approachLayers.begin(), approachLayers.end());
    arcSweeps.insert(arcSweeps.end(), approachArcSweeps.begin(), approachArcSweeps.end());

    // ══════════════════════════════════════════════════════════
    // 【阶段 5：Tap 击中扩散（layer 2, arcSweep<0）— 最顶层】
    // overlay 从 note 尺寸 UV 扩散到格边（1.0→1.28）；alpha 由 hitEffects 驱动衰减。
    // ══════════════════════════════════════════════════════════
    if (layerOverlay >= 0.0f) {
        for (const auto& hit : hitEffects) {
            if (hit.alpha <= 0.01f) continue;  // 已衰减完毕则跳过
            float cellX = 0.0f;
            float cellY = 0.0f;
            layout.cellCenter(hit.row, hit.col, activeStartCol, activeEndCol,
                              scrollOffset, cellX, cellY);
            if (!layout.cellVisible(cellX, cellY)) continue;
            pushCenteredQuad(quads, colors, layers, arcSweeps,
                             cellX, cellY, cellW, cellH,
                             1.0f, 1.0f, 1.0f, hit.alpha * m_globalAlpha, layerOverlay,
                             -(1.0f - hit.alpha));  // 负 arcSweep → fragment 扩散 UV
        }
    }
}

/// 释放 VAO/VBO 并重置 m_initialized
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

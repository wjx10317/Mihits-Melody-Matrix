// ============================================================
// renderer.cpp — 主渲染器实现
// 帧流程：renderBackground → renderGrid（占位）→ renderNotes。
// scrollOffset 在此统一计算；阵型过渡按 MatrixTransform 分发动画参数。
// ============================================================

#include "renderer.h"                    // 渲染器类声明
#include "note_renderer.h"               // Note 精灵渲染器
#include "renderer/grid_layout.h"        // 网格布局常量（格宽 gw 等）
#include "renderer/texture_cache.h"      // 纹理缓存单例
#include "beatmap/note.h"                // Note 数据结构与 MatrixTransform
#include "util/logger.h"               // 日志宏

#include <glad.h>                        // OpenGL 函数加载
#include <glm/glm.hpp>                   // 数学库
#include <glm/gtc/matrix_transform.hpp>  // ortho 等矩阵变换
#include <array>                         // std::array
#include <string>                        // std::string
#include <vector>                        // std::vector

namespace melody_matrix::renderer {

bool Renderer::init() {
    MM_LOG_INFO("Renderer", "Initializing renderer...");  // 记录初始化开始

    // ── 网格线 shader（现主要用于全屏 dim 遮罩）──
    const std::string gridVertSrc = R"(  // 顶点着色器源码：仅做正交投影变换
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const std::string gridFragSrc = R"(  // 片元着色器源码：输出 uniform 纯色
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

    auto shaderResult = Shader::compile(gridVertSrc, gridFragSrc);  // 编译 grid shader
    if (shaderResult.ok()) {
        m_gridShader = std::move(shaderResult.value());  // 成功则保存 shader 对象
    } else {
        MM_LOG_WARN("Renderer", "Grid shader failed, using fallback: " + shaderResult.error().message);  // 失败则记录警告
    }

    // ── 网格 VAO/VBO（动态顶点，用于 dim 四边形）──
    glGenVertexArrays(1, &m_gridVao);  // 创建 VAO
    glGenBuffers(1, &m_gridVbo);         // 创建 VBO

    glBindVertexArray(m_gridVao);        // 绑定 VAO 以配置顶点属性
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);  // 绑定 VBO
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 256, nullptr, GL_DYNAMIC_DRAW);  // 预分配动态顶点缓冲
    glEnableVertexAttribArray(0);      // 启用 location 0（vec2 位置）
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);  // 描述顶点布局
    glBindVertexArray(0);                // 解绑 VAO

    // ── 背景纹理 shader（全屏四边形采样 uTexture）──
    const std::string bgVertSrc = R"(  // 背景顶点着色器：传递纹理坐标
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
    const std::string bgFragSrc = R"(  // 背景片元着色器：采样纹理并乘 alpha
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uTexture;
        uniform float uAlpha;
        void main() {
            FragColor = texture(uTexture, vTexCoord) * uAlpha;
        }
    )";

    auto bgShaderResult = Shader::compile(bgVertSrc, bgFragSrc);  // 编译背景 shader
    if (bgShaderResult.ok()) {
        m_bgShader = std::move(bgShaderResult.value());  // 保存背景 shader
    } else {
        MM_LOG_WARN("Renderer", "Background shader failed: " + bgShaderResult.error().message);  // 编译失败警告
    }

    // ── 全屏背景四边形 VAO/VBO ──
    // 投影 Y 向下（top=0）。stb_flip_on_load=1 后 OpenGL v=0 为图像底部。
    // 因此屏幕 top(Y=0) 对应 v=1，bottom(Y=1080) 对应 v=0。
    float quadVertices[] = {  // 两个三角形组成全屏四边形（pos + texcoord 交错）
        // pos              // texcoord
        0.0f,    0.0f,      0.0f, 1.0f,   // top-left     -> image top-left
        1920.0f, 0.0f,      1.0f, 1.0f,   // top-right    -> image top-right
        1920.0f, 1080.0f,   1.0f, 0.0f,   // bottom-right -> image bottom-right
        0.0f,    0.0f,      0.0f, 1.0f,   // top-left     -> image top-left
        1920.0f, 1080.0f,   1.0f, 0.0f,   // bottom-right -> image bottom-right
        0.0f,    1080.0f,   0.0f, 0.0f,   // bottom-left  -> image bottom-left
    };

    glGenVertexArrays(1, &m_bgVao);  // 创建背景 VAO
    glGenBuffers(1, &m_bgVbo);       // 创建背景 VBO

    glBindVertexArray(m_bgVao);        // 绑定背景 VAO
    glBindBuffer(GL_ARRAY_BUFFER, m_bgVbo);  // 绑定背景 VBO
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);  // 上传四边形顶点（静态）

    glEnableVertexAttribArray(0);      // 启用位置属性
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);  // location 0：vec2 位置

    glEnableVertexAttribArray(1);      // 启用纹理坐标属性
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));  // location 1：vec2 UV

    glBindVertexArray(0);              // 解绑 VAO

    // ── 默认菜单背景 ──
    if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {  // 加载默认菜单背景图
        MM_LOG_WARN("Renderer", "Failed to load menu background texture — will use solid color");  // 失败则后续可能无纹理
    }

    // ── NoteRenderer 初始化 ──
    m_noteRenderer = std::make_unique<NoteRenderer>();  // 创建 Note 渲染器实例
    if (!m_noteRenderer->init()) {  // 初始化 NoteRenderer（shader、VAO 等）
        MM_LOG_WARN("Renderer", "NoteRenderer initialization failed");  // 初始化失败警告
    }

    // ── 加载 note 纹理（res/ 目录，多路径后备）──
    // 路径后备：尝试 res/、../res/、../../res/（适配不同工作目录）
    auto loadNoteTexture = [](const std::string& name) -> const Texture2D* {  // 按文件名加载 note 纹理的 lambda
        auto& cache = TextureCache::instance();  // 获取纹理缓存单例
        const std::vector<std::string> prefixes = { "res/", "../res/", "../../res/" };  // 候选路径前缀
        for (const auto& prefix : prefixes) {  // 依次尝试各前缀
            std::string path = prefix + name;  // 拼接完整路径
            const Texture2D* tex = cache.get(path);  // 先查缓存
            if (tex && tex->valid()) return tex;  // 缓存命中且有效则直接返回
            tex = cache.loadSync(path, true);  // init 必须同步可用
            if (tex && tex->valid()) {  // 加载成功
                MM_LOG_INFO("Renderer", "Loaded note texture: %s", path.c_str());  // 记录成功路径
                return tex;  // 返回纹理指针
            }
        }
        MM_LOG_WARN("Renderer", "Note texture not found: %s (fallback to solid color)", name.c_str());  // 全部失败则警告
        return nullptr;  // 返回空，NoteRenderer 将用纯色回退
    };

    const Texture2D* texTap = loadNoteTexture("tap.png");           // Tap 音符纹理
    const Texture2D* texSlider = loadNoteTexture("slider.png");       // Slider 音符纹理
    const Texture2D* texOverlay = loadNoteTexture("overlay.png");   // 叠加层纹理
    const Texture2D* texHoldPushRing = loadNoteTexture("holdpush_ring.png");  // Hold 推进环纹理
    std::array<const Texture2D*, NoteRenderer::kHoldPushStageCount> texHoldPush{};  // Hold 各进度阶段纹理数组
    static constexpr int kHoldPushPercents[NoteRenderer::kHoldPushStageCount] = {  // 各阶段对应百分比
        0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
    };
    for (int i = 0; i < NoteRenderer::kHoldPushStageCount; ++i) {  // 加载 holdpush_0~100 系列纹理
        texHoldPush[static_cast<size_t>(i)] =
            loadNoteTexture("holdpush_" + std::to_string(kHoldPushPercents[i]) + ".png");
    }
    const Texture2D* texBlock = loadNoteTexture("background.png");  // 格子背景块纹理

    if (m_noteRenderer) {  // NoteRenderer 存在则绑定纹理
        m_noteRenderer->setTextures(texTap, texSlider, texOverlay,
                                    texHoldPushRing, texHoldPush, texBlock);
    }

    m_initialized = true;  // 标记渲染器已就绪
    MM_LOG_INFO("Renderer", "Renderer initialized successfully");  // 记录初始化完成
    return true;  // 返回成功
}

void Renderer::setGameplayRendering(bool enabled) {
    m_gameplayRendering = enabled;  // 开关 gameplay 层（网格 + note）渲染
}

void Renderer::setBackgroundPath(const std::string& path) {
    if (path == m_bgPath) return;  // 路径未变则跳过
    m_bgPath = path;               // 记录新背景路径
    m_bgDirty = true;              // 标记下一帧需重新加载纹理
}

void Renderer::setFormation(int32_t rows, int32_t cols,
                            float blockSize) {
    m_gridRows = rows;       // 设置矩阵行数
    m_gridCols = cols;       // 设置矩阵列数
    m_blockSize = blockSize; // 设置单格像素尺寸
    // blockSize 同步到 NoteRenderer（格内 note/background 缩放）
    if (m_noteRenderer) {
        m_noteRenderer->setBlockSize(blockSize);  // 同步格大小到 note 渲染器
    }
    // 瞬间切换时清除过渡状态
    m_transition.active = false;  // 禁用阵型过渡动画
}

void Renderer::beginFormationTransition(int32_t prevRows, int32_t prevCols, float prevBlockSize,
                                          int32_t nextRows, int32_t nextCols, float nextBlockSize,
                                          int32_t transformType) {
    // 记录 prev/next 阵型与 v2 MatrixTransform 类型，供 renderNotes 选择动画
    m_transition.prevRows = prevRows;           // 过渡前行数
    m_transition.prevCols = prevCols;           // 过渡前列数
    m_transition.prevBlockSize = prevBlockSize; // 过渡前格大小
    m_transition.nextRows = nextRows;           // 过渡后行数
    m_transition.nextCols = nextCols;           // 过渡后列数
    m_transition.nextBlockSize = nextBlockSize; // 过渡后格大小
    m_transition.transformType = transformType; // MatrixTransform 动画类型码
    m_transition.progress = 0.0f;             // 过渡进度归零
    m_transition.active = true;               // 激活过渡状态
}

void Renderer::updateFormationTransition(float progress) {
    if (!m_transition.active) return;  // 无活跃过渡则直接返回
    m_transition.progress = std::max(0.0f, std::min(1.0f, progress));  // 将进度钳制到 [0,1]
    if (m_transition.progress >= 1.0f) {  // 过渡完成
        // 过渡完成，切换到新阵型
        m_gridRows = m_transition.nextRows;       // 应用新行数
        m_gridCols = m_transition.nextCols;       // 应用新列数
        m_blockSize = m_transition.nextBlockSize; // 应用新格大小
        m_transition.active = false;              // 关闭过渡
    }
}

void Renderer::setNotes(const std::vector<beatmap::Note>& notes, float ar) {
    m_notes = notes;  // 缓存当前帧要绘制的 note 列表
    m_ar = ar;        // 缓存 approach rate（影响 note 提前量）
}

void Renderer::setScrollState(int32_t activeStartCol, int32_t activeEndCol,
                              int32_t targetStartCol, int32_t targetEndCol,
                              bool scrolling, float scrollProgress) {
    m_activeStartCol = activeStartCol;   // 当前可见窗口起始列（滚动前/中）
    m_activeEndCol = activeEndCol;       // 当前可见窗口结束列
    m_targetStartCol = targetStartCol;   // 滚动目标窗口起始列
    m_targetEndCol = targetEndCol;       // 滚动目标窗口结束列
    m_scrolling = scrolling;             // 是否处于列滚动动画中
    m_scrollProgress = scrollProgress;   // 滚动原始进度 [0,1]（未缓动）

    // ── scrollOffset 统一计算（GridLayout::gw 为步长）──
    // 向右滚 (targetStart>activeStart) → 矩阵向左移 → scrollOffset 为负
    m_scrollOffset = 0.0f;               // 默认无水平偏移
    if (scrolling) {                     // 仅在滚动中时计算偏移
        float p = std::max(0.0f, std::min(1.0f, scrollProgress));  // 将进度钳制到 [0,1]
        // ease-in-out 缓动（与原 playing_state 计算一致）
        float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;  // 前半加速、后半减速
        float gw = GridLayout::kDefaultCellW;  // 单列像素宽度（格步长）
        int32_t colDelta = targetStartCol - activeStartCol;  // 目标列与当前列之差（正=向右滚）
        m_scrollOffset = -static_cast<float>(colDelta) * gw * easedP;  // 负号：矩阵视觉向左平移
    }
}

void Renderer::setColumnHeads(const std::array<size_t, 8>& heads, int32_t columnCount) {
    m_colHeads = heads;         // 各列 note 链表头索引（用于列内排序渲染）
    m_colHeadCount = columnCount;  // 有效列数
}

void Renderer::setBgDim(float dim) {
    m_bgDim = std::max(0.0f, std::min(1.0f, dim));  // 背景遮罩强度，钳制到 [0,1]
}

void Renderer::renderFrame(int64_t interpolatedTimeMs) {
    if (!m_initialized) return;  // 未初始化则跳过整帧

    // 延迟加载背景纹理
    if (m_bgDirty) {  // 背景路径变更后在此加载
        m_bgDirty = false;  // 清除脏标记，避免重复加载
        if (m_bgPath.empty()) {  // 空路径表示恢复默认背景
            // 恢复默认背景
            m_bgTexture = Texture2D();  // 重置纹理对象
            if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {  // 重新加载菜单背景
                MM_LOG_WARN("Renderer", "Failed to reload default background");  // 加载失败警告
            }
        } else {  // 使用指定谱面/场景背景路径
            // 优先尝试.mp4（暂不支持视频，跳过）
            // 其次加载图片
            Texture2D newTex;  // 临时纹理对象
            if (!newTex.loadFromFile(m_bgPath)) {  // 从 m_bgPath 加载图片
                MM_LOG_WARN("Renderer", "Failed to load background: " + m_bgPath);  // 加载失败
                // 加载失败则用默认
                m_bgTexture = Texture2D();  // 回退到默认纹理
                if (!m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")) {  // 尝试菜单背景
                    MM_LOG_WARN("Renderer", "Failed to reload default background");  // 默认也失败
                }
            } else {  // 自定义背景加载成功
                m_bgTexture = std::move(newTex);  // 替换当前背景纹理
                MM_LOG_INFO("Renderer", "Background loaded: " + m_bgPath);  // 记录成功
            }
        }
    }

    renderBackground();  // 绘制全屏背景（及 dim 遮罩）
    if (m_gameplayRendering) {  // gameplay 模式才绘制矩阵与 note
        renderGrid(interpolatedTimeMs);   // 网格占位（实际由 NoteRenderer 绘制块背景）
        renderNotes(interpolatedTimeMs);  // 绘制 note 与阵型/滚动动画
    }
}

void Renderer::renderBackground() {
    if (!m_bgTexture.valid() || !m_bgShader.valid()) {  // 纹理或 shader 无效则跳过
        return;
    }

    m_bgTexture.bind(0);  // 绑定背景纹理到纹理单元 0
    m_bgShader.use();     // 激活背景 shader

    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);  // 1920×1080 正交投影，Y 向下
    m_bgShader.setMat4("uProjection", &proj[0][0]);  // 上传投影矩阵
    m_bgShader.setInt("uTexture", 0);                // 告知 shader 采样单元 0
    m_bgShader.setFloat("uAlpha", 1.0f);             // 背景完全不透明

    glBindVertexArray(m_bgVao);           // 绑定背景四边形 VAO
    glDrawArrays(GL_TRIANGLES, 0, 6);     // 绘制两个三角形（6 顶点）
    glBindVertexArray(0);                 // 解绑 VAO

    Texture2D::unbind(0);  // 解绑纹理单元 0

    // ── 背景遮罩层（受 gameplayFade 控制实现休息段渐变隐藏）──
    float dimAlpha = m_bgDim * m_gameplayFade;  // 遮罩 alpha = 背景 dim × 休息段淡出系数
    if (dimAlpha > 0.001f) {  // alpha 足够大才绘制遮罩
        m_gridShader.use();  // 使用纯色 grid shader 画半透明层
        m_gridShader.setMat4("uProjection", &proj[0][0]);  // 复用同一投影矩阵
        m_gridShader.setVec4("uColor", 0.063f, 0.063f, 0.118f, dimAlpha);  // 深蓝灰半透明

        // 复用背景四边形的 VAO 绘制全屏遮罩
        // 但背景 VAO 有两个属性（pos + texcoord），grid shader 只用 location=0
        // 所以需要用 grid VAO 构建一个全屏四边形
        float dimQuad[] = {  // 仅含 vec2 位置的全屏四边形顶点
            0.0f,    0.0f,
            1920.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,    0.0f,
            1920.0f, 1080.0f,
            0.0f,    1080.0f,
        };
        glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);  // 绑定 grid 动态 VBO
        glBufferData(GL_ARRAY_BUFFER, sizeof(dimQuad), dimQuad, GL_DYNAMIC_DRAW);  // 上传遮罩四边形顶点
        glBindVertexArray(m_gridVao);              // 绑定 grid VAO（仅 pos 属性）
        glDrawArrays(GL_TRIANGLES, 0, 6);          // 绘制全屏遮罩
        glBindVertexArray(0);                        // 解绑 VAO
    }
}

void Renderer::renderGrid(int64_t /*timeMs*/) {
    // 格子 background 已改由 NoteRenderer 用 background.png 按块渲染（layer 5），
    // 活跃列高亮 / 非活跃列半透明预览；不再需要此处画网格线。
    // 阵型过渡动画也由 NoteRenderer::setAnimParams 处理。
    (void)0;  // 占位，保留接口供未来扩展
}

void Renderer::renderNotes(int64_t timeMs) {
    if (!m_noteRenderer || m_gridRows <= 0 || m_gridCols <= 0) return;  // 渲染器或阵型无效则跳过

    // 应用休息段渐变（矩阵+note 整体 alpha）
    m_noteRenderer->setGlobalAlpha(m_gameplayFade);  // 休息段时整体淡出

    if (m_transition.active) {  // 阵型过渡动画进行中
        float p = m_transition.progress;  // 原始过渡进度 [0,1]
        int32_t tt = m_transition.transformType;  // MatrixTransform 类型码

        // ease-in-out 缓动
        float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;  // 平滑插值因子

        const float PI = 3.14159265358979f;  // 圆周率，用于旋转动画

        // ── 阵型过渡动画分发（formation animation）──
        // SCALE_ONLY(100)           → blockSize 插值缩放
        // SLIDE_ROW/COL_*(201~226)  → 新行/列滑入（setAnimParams slideRows/slideCols）
        // ROTATE_*(301~399)         → 整屏旋转一周（setAnimParams rotation）
        // 其他(NONE 等)             → 中点切换 prev/next 阵型
        char cat = beatmap::MatrixTransform::category(tt);  // 取类型首字母分类（如 'R'=旋转）
        bool isSlideAdd = beatmap::MatrixTransform::isSlideRowAdd(tt) || beatmap::MatrixTransform::isSlideColAdd(tt);  // 是否为增行/增列滑入
        bool isSlideRemove = beatmap::MatrixTransform::isSlideRowRemove(tt) || beatmap::MatrixTransform::isSlideColRemove(tt);  // 是否为减行/减列滑出

        if (tt == beatmap::MatrixTransform::SCALE_ONLY) {  // 仅缩放格大小，行列不变
            // 缩放：行列不变，仅格子大小平滑变换
            float interpBlockSize = m_transition.prevBlockSize +
                (m_transition.nextBlockSize - m_transition.prevBlockSize) * easedP;  // 在 prev/next 格大小间线性插值
            m_noteRenderer->setBlockSize(interpBlockSize);  // 应用插值后的格大小
            m_noteRenderer->setAnimParams(0.0f, 1.0f, -1, -1, 1.0f, false, false);  // 无旋转/滑入，alpha=1
            m_noteRenderer->render(m_notes, timeMs, m_transition.nextRows, m_transition.nextCols, m_ar,  // 用目标行列渲染
                                   m_activeStartCol, m_activeEndCol,  // 当前可见列窗口
                                   m_colHeads, m_colHeadCount,        // 列头索引
                                   m_hitEffects,                      // 击打特效
                                   m_scrollOffset, m_scrolling, m_scrollProgress,  // 滚动状态
                                   m_targetStartCol, m_targetEndCol);  // 滚动目标列
        } else if (isSlideAdd) {  // 增行或增列滑入动画
            // 滑入：新行从左/新列从顶部（按宏判断轴）
            bool slideRows = beatmap::MatrixTransform::isSlideRowAdd(tt);  // 是否行方向滑入
            bool slideCols = beatmap::MatrixTransform::isSlideColAdd(tt);  // 是否列方向滑入
            m_noteRenderer->setBlockSize(m_transition.nextBlockSize);  // 使用目标格大小
            m_noteRenderer->setAnimParams(0.0f, 1.0f,
                                          m_transition.prevRows, m_transition.prevCols,  // 旧阵型尺寸（滑入基准）
                                          easedP, slideRows, slideCols);  // 滑入进度与轴向
            m_noteRenderer->render(m_notes, timeMs, m_transition.nextRows, m_transition.nextCols, m_ar,  // 目标阵型
                                   m_activeStartCol, m_activeEndCol,
                                   m_colHeads, m_colHeadCount,
                                   m_hitEffects,
                                   m_scrollOffset, m_scrolling, m_scrollProgress,
                                   m_targetStartCol, m_targetEndCol);
        } else if (isSlideRemove) {  // 减行或减列：旧阵淡出 + 新阵淡入
            // 滑出：旧矩阵淡出，新矩阵淡入（行列减少时使用）
            if (p < 0.5f) {  // 前半段：渲染旧阵型并淡出
                m_noteRenderer->setBlockSize(m_transition.prevBlockSize);  // 旧格大小
                m_noteRenderer->setAnimParams(0.0f, 1.0f - easedP * 2.0f, -1, -1, 1.0f, false, false);  // alpha 从 1→0
                m_noteRenderer->render(m_notes, timeMs, m_transition.prevRows, m_transition.prevCols, m_ar,  // 旧行列
                                       m_activeStartCol, m_activeEndCol,
                                       m_colHeads, m_colHeadCount,
                                       m_hitEffects,
                                       m_scrollOffset, m_scrolling, m_scrollProgress,
                                       m_targetStartCol, m_targetEndCol);
            } else {  // 后半段：渲染新阵型并淡入
                m_noteRenderer->setBlockSize(m_transition.nextBlockSize);  // 新格大小
                m_noteRenderer->setAnimParams(0.0f, (easedP - 0.5f) * 2.0f, -1, -1, 1.0f, false, false);  // alpha 从 0→1
                m_noteRenderer->render(m_notes, timeMs, m_transition.nextRows, m_transition.nextCols, m_ar,  // 新行列
                                       m_activeStartCol, m_activeEndCol,
                                       m_colHeads, m_colHeadCount,
                                       m_hitEffects,
                                       m_scrollOffset, m_scrolling, m_scrollProgress,
                                       m_targetStartCol, m_targetEndCol);
            }
        } else if (cat == 'R') {  // 旋转类 MatrixTransform
            // 旋转一周：前半段旧矩阵旋转0→180°淡出，后半段新矩阵旋转180°→360°淡入
            float angle = p * 2.0f * PI;  // 整段过渡对应 0→2π 弧度
            if (p < 0.5f) {  // 前半：旧阵型旋转并淡出
                m_noteRenderer->setBlockSize(m_transition.prevBlockSize);  // 旧格大小
                m_noteRenderer->setAnimParams(angle, 1.0f - easedP * 2.0f, -1, -1, 1.0f, false, false);  // 旋转角 + 递减 alpha
                m_noteRenderer->render(m_notes, timeMs, m_transition.prevRows, m_transition.prevCols, m_ar,
                                       m_activeStartCol, m_activeEndCol,
                                       m_colHeads, m_colHeadCount,
                                       m_hitEffects,
                                       m_scrollOffset, m_scrolling, m_scrollProgress,
                                       m_targetStartCol, m_targetEndCol);
            } else {  // 后半：新阵型继续旋转并淡入
                m_noteRenderer->setBlockSize(m_transition.nextBlockSize);  // 新格大小
                m_noteRenderer->setAnimParams(angle, (easedP - 0.5f) * 2.0f, -1, -1, 1.0f, false, false);  // 同角度 + 递增 alpha
                m_noteRenderer->render(m_notes, timeMs, m_transition.nextRows, m_transition.nextCols, m_ar,
                                       m_activeStartCol, m_activeEndCol,
                                       m_colHeads, m_colHeadCount,
                                       m_hitEffects,
                                       m_scrollOffset, m_scrolling, m_scrollProgress,
                                       m_targetStartCol, m_targetEndCol);
            }
        } else {  // NONE 等默认：中点硬切换 prev/next
            // 默认（NONE 等）：切换式
            int32_t rows = p < 0.5f ? m_transition.prevRows : m_transition.nextRows;  // 前半用旧行数，后半用新行数
            int32_t cols = p < 0.5f ? m_transition.prevCols : m_transition.nextCols;  // 前半用旧列数，后半用新列数
            m_noteRenderer->setBlockSize(p < 0.5f ? m_transition.prevBlockSize : m_transition.nextBlockSize);  // 对应格大小
            m_noteRenderer->setAnimParams(0.0f, 1.0f, -1, -1, 1.0f, false, false);  // 无额外动画，alpha=1
            m_noteRenderer->render(m_notes, timeMs, rows, cols, m_ar,  // 按当前半段阵型渲染
                                   m_activeStartCol, m_activeEndCol,
                                   m_colHeads, m_colHeadCount,
                                   m_hitEffects,
                                   m_scrollOffset, m_scrolling, m_scrollProgress,
                                   m_targetStartCol, m_targetEndCol);
        }
    } else {  // 无阵型过渡：稳态渲染
        m_noteRenderer->setBlockSize(m_blockSize);  // 当前格大小
        m_noteRenderer->setAnimParams(0.0f, 1.0f, -1, -1, 1.0f, false, false);  // 默认无动画参数
        m_noteRenderer->render(m_notes, timeMs, m_gridRows, m_gridCols, m_ar,  // 当前阵型与 note 列表
                               m_activeStartCol, m_activeEndCol,
                               m_colHeads, m_colHeadCount,
                               m_hitEffects,
                               m_scrollOffset, m_scrolling, m_scrollProgress,  // 列滚动偏移与状态
                               m_targetStartCol, m_targetEndCol);
    }
}

void Renderer::shutdown() {
    m_noteRenderer.reset();  // 释放 NoteRenderer 及其 OpenGL 资源

    m_bgTexture = Texture2D();  // 释放背景纹理
    if (m_bgVao != 0) {  // 若背景 VAO 已创建
        glDeleteVertexArrays(1, &m_bgVao);  // 删除 VAO
        m_bgVao = 0;  // 句柄归零
    }
    if (m_bgVbo != 0) {  // 若背景 VBO 已创建
        glDeleteBuffers(1, &m_bgVbo);  // 删除 VBO
        m_bgVbo = 0;  // 句柄归零
    }

    if (m_gridVao != 0) {  // 若 grid VAO 已创建
        glDeleteVertexArrays(1, &m_gridVao);  // 删除 grid VAO
        m_gridVao = 0;  // 句柄归零
    }
    if (m_gridVbo != 0) {  // 若 grid VBO 已创建
        glDeleteBuffers(1, &m_gridVbo);  // 删除 grid VBO
        m_gridVbo = 0;  // 句柄归零
    }
    m_initialized = false;  // 标记未初始化
    MM_LOG_INFO("Renderer", "Renderer shut down");  // 记录关闭完成
}

} // namespace melody_matrix::renderer

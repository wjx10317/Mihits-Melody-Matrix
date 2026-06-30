#pragma once

// ============================================================
// note_renderer.h — 音符与格子背景实例化渲染器
// 使用 OpenGL 实例化绘制（glDrawArraysInstanced）：共享单位四边形 VBO，
// 每实例携带位置/尺寸、颜色、纹理层 ID、缩圈/击中 UV 参数。
// 纹理层 ID 在 fragment shader 中映射到不同 sampler2D。
// ============================================================

#include "renderer/shader.h"
#include "renderer/texture.h"
#include "beatmap/note.h"
#include "beatmap/beatmap.h"
#include "renderer/grid_layout.h"

#include <array>
#include <vector>
#include <cstdint>

namespace melody_matrix::renderer {

/// Tap 击中扩散特效：格坐标 + 当前 alpha（由 playing_state 驱动衰减）
struct CellHitEffect {
    int32_t col = 0;
    int32_t row = 0;
    float alpha = 0.0f;
};

/// 音符/格子实例化渲染器。
///
/// 纹理层 ID（aTexLayer → fragment shader 分支 → OpenGL 纹理单元）:
///   层 5  block      → uTexBlock       (纹理单元 15)
///   层 0  tap        → uTexTap         (纹理单元 0)
///   层 1  slider     → uTexSlider      (纹理单元 1)
///   层 2  overlay    → uTexOverlay     (纹理单元 2)  缩圈 / 击中扩散
///   层 3  holdpush_ring → uTexHoldPushRing (纹理单元 3)
///   层 6..16 holdpush 0/10/..../100 → uTexHoldPush0..10 (纹理单元 4..14)
class NoteRenderer {
public:
    static constexpr int kHoldPushStageCount = 11;

    NoteRenderer() = default;
    ~NoteRenderer() = default;

    /// 编译 note 实例化 shader，创建 VAO/VBO（单位四边形 + 4 路实例属性）
    bool init();

    /// 每帧构建实例数据并 glDrawArraysInstanced 一次绘制全部 note/格子
    void render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                int rows, int cols, float ar,
                int32_t activeStartCol, int32_t activeEndCol,
                const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                const std::vector<CellHitEffect>& hitEffects = {},
                float scrollOffset = 0.0f, bool scrolling = false, float scrollProgress = 0.0f,
                int32_t targetStartCol = 0, int32_t targetEndCol = 3);

    void shutdown();

    /// 注入各层纹理指针（可为 nullptr，对应层跳过绘制）
    void setTextures(const Texture2D* tap, const Texture2D* slider,
                     const Texture2D* overlay,
                     const Texture2D* holdPushRing,
                     const std::array<const Texture2D*, kHoldPushStageCount>& holdPushStages,
                     const Texture2D* block);

    /// Formation.blockSize：格内 note/background 同步缩放
    void setBlockSize(float blockSize) { m_blockSize = blockSize; }

    /// 全局 alpha（休息段 gameplayFade 等）
    void setGlobalAlpha(float alpha) { m_globalAlpha = std::max(0.0f, std::min(1.0f, alpha)); }

    /// 阵型过渡动画参数（由 Renderer::renderNotes 按 MatrixTransform 类型设置）
    void setAnimParams(float rotation, float alpha,
                       int32_t prevRows, int32_t prevCols,
                       float slideProgress, bool slideRows, bool slideCols) {
        m_animRotation = rotation;
        m_animAlpha = alpha;
        m_animPrevRows = prevRows;
        m_animPrevCols = prevCols;
        m_animSlideProgress = slideProgress;
        m_animSlideRows = slideRows;
        m_animSlideCols = slideCols;
    }

private:
    /// background 与 note 同步按 blockSize 缩放；格缝露出 Background Dim。
    static constexpr float kCellTexRefPx = GridLayout::kDefaultCellW;
    static constexpr float kLayerTap = 0.0f;
    static constexpr float kLayerSlider = 1.0f;
    static constexpr float kLayerOverlay = 2.0f;
    static constexpr float kLayerHoldPushRing = 3.0f;
    static constexpr float kLayerHoldPushBase = 6.0f;   ///< holdpush_0 对应层 6
    static constexpr float kLayerBlock = 5.0f;

    /// 构建本帧全部实例顶点数据（见 .cpp 绘制顺序说明）
    void buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           const std::vector<CellHitEffect>& hitEffects,
                           std::vector<float>& quads,
                           std::vector<float>& colors,
                           std::vector<float>& layers,
                           std::vector<float>& arcSweeps,
                           float scrollOffset, bool scrolling, float scrollProgress,
                           int32_t targetStartCol, int32_t targetEndCol);

    /// 追加一个轴对齐四边形实例（左上角 + 宽高）
    void pushQuad(std::vector<float>& quads, std::vector<float>& colors,
                  std::vector<float>& layers, std::vector<float>& arcSweeps,
                  float x, float y, float w, float h,
                  float r, float g, float b, float a,
                  float layer, float arcSweep = 0.0f) const;

    /// 追加一个以 (cx,cy) 为中心的四边形实例
    void pushCenteredQuad(std::vector<float>& quads, std::vector<float>& colors,
                          std::vector<float>& layers, std::vector<float>& arcSweeps,
                          float cx, float cy, float w, float h,
                          float r, float g, float b, float a, float layer,
                          float arcSweep = 0.0f) const;

    /// Hold 进度 [0,1] → holdpush 纹理层（6 + stage）
    float holdPushLayerForProgress(float progress) const;

    /// 检查指定层是否有可用纹理
    bool holdPushLayerHasTexture(float layer) const;

    static constexpr float kApproachRingUvOuter = 1.35f;  ///< 缩圈起点 UV 缩放（越大环越大）
    static constexpr float kHitRingUvExpand = 1.28f;      ///< 击中扩散终点 UV 缩放

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_quadVbo = 0;       ///< 共享单位四边形（2 三角，6 顶点）
    uint32_t m_instanceVbo = 0;   ///< 每实例 vec4: xy=左上角, zw=宽高
    uint32_t m_colorVbo = 0;      ///< 每实例 vec4 RGBA
    uint32_t m_layerVbo = 0;      ///< 每实例 float 纹理层 ID
    uint32_t m_arcVbo = 0;        ///< 每实例 float 缩圈/击中 UV 参数（aArcSweep）
    Shader m_shader;
    int32_t m_maxInstances = 1536;

    const Texture2D* m_texTap = nullptr;
    const Texture2D* m_texSlider = nullptr;
    const Texture2D* m_texOverlay = nullptr;
    const Texture2D* m_texHoldPushRing = nullptr;
    std::array<const Texture2D*, kHoldPushStageCount> m_texHoldPush{};
    const Texture2D* m_texBlock = nullptr;

    float m_blockSize = 1.0f;
    float m_globalAlpha = 1.0f;

    // ── 阵型过渡动画（formation animation）──
    float m_animRotation = 0.0f;      ///< 整屏旋转（ROTATE_* 过渡）
    float m_animAlpha = 1.0f;         ///< 过渡淡出 alpha
    int32_t m_animPrevRows = -1;      ///< 滑入时旧行数边界
    int32_t m_animPrevCols = -1;      ///< 滑入时旧列数边界
    float m_animSlideProgress = 1.0f; ///< 滑入进度 [0,1]
    bool m_animSlideRows = false;     ///< 新行从左滑入
    bool m_animSlideCols = false;     ///< 新列从上滑入
};

} // namespace melody_matrix::renderer

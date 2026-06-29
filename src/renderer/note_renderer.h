#pragma once

#include "renderer/shader.h"
#include "renderer/texture.h"
#include "beatmap/note.h"
#include "beatmap/beatmap.h"
#include "renderer/grid_layout.h"

#include <array>
#include <vector>
#include <cstdint>

namespace melody_matrix::renderer {

struct CellHitEffect {
    int32_t col = 0;
    int32_t row = 0;
    float alpha = 0.0f;
};

/// 纹理层 ID（fragment shader 按层选纹理）:
///   5 block | 0 tap | 1 slider | 2 overlay 缩圈
///   3 holdpush_ring | 6..16 = holdpush 0/10/..../100
class NoteRenderer {
public:
    static constexpr int kHoldPushStageCount = 11;

    NoteRenderer() = default;
    ~NoteRenderer() = default;

    bool init();
    void render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                int rows, int cols, float ar,
                int32_t activeStartCol, int32_t activeEndCol,
                const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                const std::vector<CellHitEffect>& hitEffects = {},
                float scrollOffset = 0.0f, bool scrolling = false, float scrollProgress = 0.0f,
                int32_t targetStartCol = 0, int32_t targetEndCol = 3);
    void shutdown();

    void setTextures(const Texture2D* tap, const Texture2D* slider,
                     const Texture2D* overlay,
                     const Texture2D* holdPushRing,
                     const std::array<const Texture2D*, kHoldPushStageCount>& holdPushStages,
                     const Texture2D* block);

    void setBlockSize(float blockSize) { m_blockSize = blockSize; }
    void setGlobalAlpha(float alpha) { m_globalAlpha = std::max(0.0f, std::min(1.0f, alpha)); }

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
    static constexpr float kLayerHoldPushBase = 6.0f;
    static constexpr float kLayerBlock = 5.0f;

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

    void pushQuad(std::vector<float>& quads, std::vector<float>& colors,
                  std::vector<float>& layers, std::vector<float>& arcSweeps,
                  float x, float y, float w, float h,
                  float r, float g, float b, float a,
                  float layer, float arcSweep = 0.0f) const;

    void pushCenteredQuad(std::vector<float>& quads, std::vector<float>& colors,
                          std::vector<float>& layers, std::vector<float>& arcSweeps,
                          float cx, float cy, float w, float h,
                          float r, float g, float b, float a, float layer,
                          float arcSweep = 0.0f) const;

    float holdPushLayerForProgress(float progress) const;
    bool holdPushLayerHasTexture(float layer) const;

    static constexpr float kApproachRingUvOuter = 1.35f;  ///< 缩圈起点 UV（越大环越大）
    static constexpr float kHitRingUvExpand = 1.28f;      ///< 击中扩散终点 UV

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_quadVbo = 0;
    uint32_t m_instanceVbo = 0;
    uint32_t m_colorVbo = 0;
    uint32_t m_layerVbo = 0;
    uint32_t m_arcVbo = 0;
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

    float m_animRotation = 0.0f;
    float m_animAlpha = 1.0f;
    int32_t m_animPrevRows = -1;
    int32_t m_animPrevCols = -1;
    float m_animSlideProgress = 1.0f;
    bool m_animSlideRows = false;
    bool m_animSlideCols = false;
};

} // namespace melody_matrix::renderer

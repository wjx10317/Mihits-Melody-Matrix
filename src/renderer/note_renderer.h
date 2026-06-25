#pragma once

#include "renderer/shader.h"
#include "renderer/texture.h"
#include "beatmap/note.h"
#include "beatmap/beatmap.h"
#include "gameplay/judge_strategy.h"

#include <vector>
#include <cstdint>

namespace melody_matrix::renderer {

/// 音符渲染器 — 在网格上绘制点音符方块和长按音符条。
/// 使用实例化渲染 + 多纹理绑定（通过实例纹理层ID选择）。
/// 纹理层定义：
///   5 = background.png (判定矩阵每个格子的背景块，256x256 缩放到 gw x gh)
///   0 = tap.png (点音符本体)
///   1 = slider.png (长按音符本体)
///   2 = overlay.png (判定环/缩圈)
///   3 = sliderpush_ring.png (slider 进度环底图)
///   4 = sliderpush_100.png (slider 100% 进度高亮)
///   -1 = 纯色（无纹理，用 vColor 直接着色，用于未加载纹理时的 fallback）
/// 绘制顺序：block(5) 先画（底层），note/overlay 后画（上层），靠 layer 区分纹理，单次 draw call
class NoteRenderer {
public:
    NoteRenderer() = default;
    ~NoteRenderer() = default;

    bool init();
    void render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                int rows, int cols, float ar,
                int32_t activeStartCol, int32_t activeEndCol,
                const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                float scrollOffset = 0.0f, bool scrolling = false, float scrollProgress = 0.0f,
                int32_t targetStartCol = 0, int32_t targetEndCol = 3);
    void shutdown();

    /// 设置 note 渲染所需的纹理资源（由 Renderer 在初始化时调用）
    /// 指针可为 nullptr（对应层退化为纯色）
    /// block 纹理 = background.png，用于判定矩阵每个格子的背景块
    void setTextures(const Texture2D* tap, const Texture2D* slider,
                     const Texture2D* overlay, const Texture2D* sliderPushRing,
                     const Texture2D* sliderPushFull, const Texture2D* block);

    /// 设置 note 图片相对格子的缩放比例（来自 Formation.blockSize，默认 0.9）
    void setBlockSize(float blockSize) { m_blockSize = blockSize; }

    /// 设置全局 alpha（用于休息段渐变隐藏，0=隐藏, 1=显示，默认1）
    void setGlobalAlpha(float alpha) { m_globalAlpha = std::max(0.0f, std::min(1.0f, alpha)); }

private:
    void buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           std::vector<float>& quads,
                           std::vector<float>& colors,
                           std::vector<float>& layers,
                           float scrollOffset = 0.0f, bool scrolling = false, float scrollProgress = 0.0f,
                           int32_t targetStartCol = 0, int32_t targetEndCol = 3);

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_quadVbo = 0;    // 单位四边形（共享几何体，含 UV）
    uint32_t m_instanceVbo = 0; // 实例数据（位置+大小）
    uint32_t m_colorVbo = 0;   // 实例颜色（tint + alpha 调制）
    uint32_t m_layerVbo = 0;   // 实例纹理层 ID
    Shader m_shader;
    int32_t m_maxInstances = 1024;  ///< block背景(rows*cols) + note + overlay 总实例数上限

    // 纹理资源（非拥有指针，由 TextureCache 管理生命周期）
    const Texture2D* m_texTap = nullptr;
    const Texture2D* m_texSlider = nullptr;
    const Texture2D* m_texOverlay = nullptr;
    const Texture2D* m_texSliderPushRing = nullptr;
    const Texture2D* m_texSliderPushFull = nullptr;
    const Texture2D* m_texBlock = nullptr;  ///< background.png，判定矩阵格子背景块

    // note 图片相对格子的缩放比例（来自 Formation.blockSize）
    float m_blockSize = 0.9f;
    float m_globalAlpha = 1.0f;  ///< 全局 alpha（休息段渐变用）
};

} // namespace melody_matrix::renderer

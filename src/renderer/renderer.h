#pragma once

#include "renderer/shader.h"
#include "renderer/texture.h"
#include "renderer/note_renderer.h"
#include "beatmap/note.h"
#include "beatmap/beatmap.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace melody_matrix::renderer {

/// 渲染层顺序（从后到前）
enum class RenderLayer { Background = 0, Grid = 1, Notes = 2, Border = 3, Effect = 4 };

/// 阵型过渡渲染状态
struct FormationTransition {
    int32_t prevRows = 0;
    int32_t prevCols = 0;
    int32_t nextRows = 0;
    int32_t nextCols = 0;
    float progress = 0.0f;     ///< 0.0 = 旧阵型, 1.0 = 新阵型
    bool active = false;
};

} // namespace melody_matrix::renderer

namespace melody_matrix::renderer {

/// 主渲染器 — 管理渲染管线和 GL 状态。
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init();
    void renderFrame(int64_t interpolatedTimeMs);
    void setGameplayRendering(bool enabled);
    void setBackgroundPath(const std::string& path);
    void setFormation(int32_t rows, int32_t cols,
                      float blockSize = 0.9f,
                      beatmap::NoteTransformType noteTransformType = beatmap::NoteTransformType::Scale);
    void beginFormationTransition(int32_t prevRows, int32_t prevCols,
                                   int32_t nextRows, int32_t nextCols);
    void updateFormationTransition(float progress);
    void setNotes(const std::vector<beatmap::Note>& notes, float ar);
    /// 设置滚动状态。scrollOffset 由 renderer 内部根据 scrollProgress 和 m_gridCols 统一计算，
    /// 确保 renderGrid / renderNotes / note_renderer 三处 gw 基准完全一致，消除抽搐和错位。
    void setScrollState(int32_t activeStartCol, int32_t activeEndCol,
                        int32_t targetStartCol, int32_t targetEndCol,
                        bool scrolling, float scrollProgress);
    /// 更新各列判定头指针（用于跳过已判定的音符）
    void setColumnHeads(const std::array<size_t, 8>& heads, int32_t columnCount);
    void shutdown();

    /// 设置背景遮罩透明度（0.0=无遮罩, 1.0=完全遮盖）
    void setBgDim(float dim);

private:
    void renderBackground();
    void renderGrid(int64_t timeMs);
    void renderNotes(int64_t timeMs);

    bool m_initialized = false;
    bool m_gameplayRendering = false;

    uint32_t m_gridVao = 0;
    uint32_t m_gridVbo = 0;
    Shader m_gridShader;

    Texture2D m_bgTexture;
    Shader m_bgShader;
    uint32_t m_bgVao = 0;
    uint32_t m_bgVbo = 0;
    std::string m_bgPath;
    bool m_bgDirty = false;

    std::unique_ptr<NoteRenderer> m_noteRenderer;
    std::vector<beatmap::Note> m_notes;
    float m_ar = 5.0f;

    int32_t m_gridRows = 3;
    int32_t m_gridCols = 4;
    float m_blockSize = 0.9f;  ///< note图片相对格子的缩放比例（来自 Formation.blockSize）
    beatmap::NoteTransformType m_noteTransformType = beatmap::NoteTransformType::Scale;  ///< note图片出现方式

    FormationTransition m_transition;

    float m_bgDim = 0.67f;  ///< 背景遮罩透明度（默认67%）

    // ── 列活跃状态 ──
    int32_t m_activeStartCol = 0;
    int32_t m_activeEndCol = 3;
    float m_scrollOffset = 0.0f;          ///< 滚动偏移（像素）
    int32_t m_targetStartCol = 0;         ///< 目标起始列
    int32_t m_targetEndCol = 3;           ///< 目标结束列
    bool m_scrolling = false;             ///< 是否正在滚动
    float m_scrollProgress = 0.0f;        ///< 滚动进度 [0,1]

    // ── 列判定头指针 ──
    std::array<size_t, 8> m_colHeads = {};
    int32_t m_colHeadCount = 0;
};

} // namespace melody_matrix::renderer

#pragma once

// ============================================================
// renderer.h — 主渲染器
// 管理渲染管线：背景 → 格子/note（NoteRenderer 实例化）→ 遮罩。
// 统一计算 scrollOffset / blockSize / 阵型过渡动画，再下发给 NoteRenderer。
// ============================================================

#include "renderer/shader.h"        // Shader
#include "renderer/texture.h"       // Texture2D
#include "renderer/note_renderer.h" // NoteRenderer, CellHitEffect
#include "beatmap/note.h"           // beatmap::Note
#include "beatmap/beatmap.h"        // MatrixTransform 等

#include <array>    // m_colHeads
#include <cstdint>  // int32_t, int64_t
#include <memory>   // unique_ptr
#include <string>   // 背景路径
#include <vector>   // m_notes, m_hitEffects

namespace melody_matrix::renderer {

/// 渲染层顺序（从后到前，概念分层；实际合批在 NoteRenderer 内完成）
enum class RenderLayer { Background = 0, Grid = 1, Notes = 2, Border = 3, Effect = 4 };

/// 阵型过渡状态（Formation 切换时的 prev/next 及动画进度）
struct FormationTransition {
    int32_t prevRows = 0;           ///< 过渡前矩阵行数
    int32_t prevCols = 0;             ///< 过渡前矩阵列数
    int32_t nextRows = 0;             ///< 过渡后矩阵行数
    int32_t nextCols = 0;             ///< 过渡后矩阵列数
    float prevBlockSize = 1.0f;       ///< 过渡前格内缩放
    float nextBlockSize = 1.0f;       ///< 过渡后格内缩放
    int32_t transformType = 0;        ///< v2 宏编号（见 beatmap::MatrixTransform）
    float progress = 0.0f;            ///< 0.0 = 旧阵型, 1.0 = 新阵型
    bool active = false;              ///< 是否正在播放过渡动画
};

} // namespace melody_matrix::renderer

namespace melody_matrix::renderer {

/// 主渲染器 — 管理 OpenGL 状态、背景、NoteRenderer 与阵型/滚动参数。
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    /// 初始化 grid/bg shader、全屏四边形 VAO、NoteRenderer 及 note 纹理
    bool init();

    /// 每帧入口：背景 →（gameplay 时）格子占位 + note 实例化绘制
    void renderFrame(int64_t interpolatedTimeMs);

    void setGameplayRendering(bool enabled);              ///< 是否绘制 gameplay 层
    void setBackgroundPath(const std::string& path);      ///< 设置/切换背景图路径

    /// 瞬间设置阵型（无过渡动画）
    void setFormation(int32_t rows, int32_t cols,
                      float blockSize = 1.0f);

    /// 开始阵型过渡（prev → next，transformType 决定动画类型）
    void beginFormationTransition(int32_t prevRows, int32_t prevCols, float prevBlockSize,
                                  int32_t nextRows, int32_t nextCols, float nextBlockSize,
                                  int32_t transformType);

    /// 更新过渡进度；progress>=1 时切换到 next 阵型
    void updateFormationTransition(float progress);

    void setNotes(const std::vector<beatmap::Note>& notes, float ar);  ///< 本帧待绘 note 与 AR

    /// 设置列滚动状态。scrollOffset 在此统一计算（ease-in-out × gw），
    /// 确保 NoteRenderer 与 GridLayout 使用相同基准，消除滚动抽搐。
    void setScrollState(int32_t activeStartCol, int32_t activeEndCol,
                        int32_t targetStartCol, int32_t targetEndCol,
                        bool scrolling, float scrollProgress);

    /// 更新各列判定头指针（NoteRenderer 用于跳过已判定 note）
    void setColumnHeads(const std::array<size_t, 8>& heads, int32_t columnCount);
    void setHitEffects(const std::vector<CellHitEffect>& effects) { m_hitEffects = effects; }
    void shutdown();  ///< 释放 GL 资源

    /// 背景遮罩透明度（0.0=无遮罩, 1.0=完全遮盖）
    void setBgDim(float dim);

    /// 游戏界面整体淡入淡出（休息段：>10s 空挡隐藏，新 note 前 3s 恢复）
    void setGameplayFade(float fade) { m_gameplayFade = std::max(0.0f, std::min(1.0f, fade)); }

private:
    void renderBackground();              ///< 全屏背景 + dim 遮罩
    void renderGrid(int64_t timeMs);      ///< 格子层（现由 NoteRenderer 承担）
    void renderNotes(int64_t timeMs);     ///< note 实例化绘制入口

    bool m_initialized = false;           ///< init() 是否成功
    bool m_gameplayRendering = false;     ///< 是否绘制 gameplay

    uint32_t m_gridVao = 0;               ///< dim 四边形 VAO
    uint32_t m_gridVbo = 0;               ///< dim 四边形 VBO
    Shader m_gridShader;                  ///< 纯色 shader（dim 用）

    Texture2D m_bgTexture;                ///< 当前背景纹理
    Shader m_bgShader;                    ///< 背景采样 shader
    uint32_t m_bgVao = 0;                 ///< 全屏背景四边形 VAO
    uint32_t m_bgVbo = 0;                 ///< 全屏背景四边形 VBO
    std::string m_bgPath;                 ///< 待加载背景路径
    bool m_bgDirty = false;               ///< 路径变更后需重载纹理

    std::unique_ptr<NoteRenderer> m_noteRenderer;  ///< note/格子实例化渲染器
    std::vector<beatmap::Note> m_notes;          ///< 当前谱面 note 列表
    float m_ar = 5.0f;                           ///< Approach Rate

    int32_t m_gridRows = 3;               ///< 当前阵型行数
    int32_t m_gridCols = 4;               ///< 当前阵型列数
    float m_blockSize = 1.0f;             ///< Formation.blockSize，同步到 NoteRenderer
    FormationTransition m_transition;     ///< 阵型过渡动画状态

    float m_bgDim = 0.67f;                ///< 背景 dim 强度
    float m_gameplayFade = 1.0f;          ///< gameplay 整体 alpha

    // ── 列活跃与滚动 ──
    int32_t m_activeStartCol = 0;         ///< 当前活跃窗起始列
    int32_t m_activeEndCol = 3;           ///< 当前活跃窗结束列
    float m_scrollOffset = 0.0f;          ///< 像素水平偏移（负=矩阵左移）
    int32_t m_targetStartCol = 0;         ///< 滚动目标起始列
    int32_t m_targetEndCol = 3;           ///< 滚动目标结束列
    bool m_scrolling = false;             ///< 是否正在列滚动
    float m_scrollProgress = 0.0f;        ///< 滚动进度 [0,1]

    // ── 列判定头指针 ──
    std::array<size_t, 8> m_colHeads = {};  ///< 每列已判定 note 计数
    int32_t m_colHeadCount = 0;             ///< 有效列数
    std::vector<CellHitEffect> m_hitEffects; ///< Tap 击中扩散特效
};

} // namespace melody_matrix::renderer

#pragma once

#include "renderer/shader.h"
#include "renderer/texture.h"
#include <cstdint>
#include <memory>

namespace melody_matrix::renderer {

/// 渲染层顺序（从后到前）
enum class RenderLayer { Background = 0, Grid = 1, Border = 2, Effect = 3 };

/// 主渲染器 — 管理渲染管线和 GL 状态。
/// 按顺序渲染层：背景 → 网格 → 边框 → 特效。
/// 第 4 层（ImGui）由 UIManager 在 Renderer 之后处理。
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    /// 初始化渲染器资源（着色器、VAO 等）
    bool init();

    /// 渲染一帧（以显示器刷新率调用）
    void renderFrame(int64_t interpolatedTimeMs);

    /// 启用或禁用游戏玩法渲染（网格、音符等）
    /// 仅在 Playing/Paused 状态时为 true。
    void setGameplayRendering(bool enabled);

    /// 清理 GL 资源
    void shutdown();

private:
    void renderBackground();
    void renderGrid(int64_t timeMs);

    bool m_initialized = false;
    bool m_gameplayRendering = false;

    // Grid rendering resources
    uint32_t m_gridVao = 0;
    uint32_t m_gridVbo = 0;
    Shader m_gridShader;
    Shader m_fallbackShader;

    // Background rendering resources
    Texture2D m_bgTexture;
    Shader m_bgShader;
    uint32_t m_bgVao = 0;
    uint32_t m_bgVbo = 0;

    // Current formation (default 4x3 for now)
    int32_t m_gridRows = 3;
    int32_t m_gridCols = 4;
};

} // namespace melody_matrix::renderer

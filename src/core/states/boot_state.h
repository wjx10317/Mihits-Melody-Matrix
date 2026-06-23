#pragma once

#include "core/game_state_base.h"
#include "core/asset_loader.h"

namespace melody_matrix::core {

/// 启动状态：播放加载动画，异步预加载资源，完成后过渡到主菜单。
class BootState : public GameStateBase {
public:
    BootState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

private:
    void renderLoadingAnimation();
    void renderProgressBar(float progress);

    // ── 动画状态 ──
    float m_time = 0.0f;            ///< 进入 BootState 后的累计时间
    bool m_animationDone = false;    ///< 动画是否播完
    bool m_transitionRequested = false; ///< 是否已请求过渡到主菜单

    // ── 异步加载 ──
    AssetLoader m_loader;
    bool m_loaderStarted = false;
    bool m_texturesLoaded = false;  ///< 主线程纹理加载是否完成

    // ── 动画参数 ──
    static constexpr float M_DRAW_DURATION = 1.2f;   ///< "M" 路径绘制时长
    static constexpr float I_STEM_DURATION = 0.4f;    ///< "i" 竖线绘制时长
    static constexpr float I_DOT_DURATION = 0.2f;     ///< "i" 点绘制时长
    static constexpr float HITS_SLIDE_DURATION = 0.8f;///< "hits" 滑入时长
    static constexpr float MINIMUM_DURATION = M_DRAW_DURATION + I_STEM_DURATION + I_DOT_DURATION + HITS_SLIDE_DURATION;
};

} // namespace melody_matrix::core

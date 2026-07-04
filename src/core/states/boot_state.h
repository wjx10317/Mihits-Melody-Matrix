// ============================================================
// boot_state.h — 启动状态
//
// 首个状态：品牌动画 + 后台 scanBeatmaps（含按组纹理解码）→ MainMenu
// ============================================================
#pragma once

#include "core/game_state_base.h"
#include "core/asset_loader.h"

#include <string>
#include <vector>

namespace melody_matrix::core {

/// 启动状态：播放加载动画，异步预加载资源，完成后过渡到主菜单。
class BootState : public GameStateBase {
public:
    BootState() = default;

    /// 进入状态：重置动画与加载标志
    void onEnter() override;
    /// 退出状态：等待异步加载线程结束
    void onExit() override;
    /// 每帧更新：驱动异步扫描、纹理预加载与状态过渡
    GameState update(float dt) override;
    /// 渲染加载动画与进度条
    void render() override;

private:
    /// 绘制 "Mi hits" 品牌加载动画（M 路径 / i 竖线 / i 点 / hits 滑入）
    void renderLoadingAnimation();
    /// 绘制底部渐变进度条与百分比文字
    void renderProgressBar(float progress);

    // ── 动画状态 ──
    float m_time = 0.0f;            ///< 进入 BootState 后的累计时间
    bool m_animationDone = false;    ///< 动画是否播完
    bool m_transitionRequested = false; ///< 是否已请求过渡到主菜单

    // ── 异步加载 ──
    AssetLoader m_loader;
    bool m_loaderStarted = false;
    bool m_preloadRequested = false; ///< 扫描完成后是否已收集待就绪的背景图路径
    bool m_texturesLoaded = false;   ///< 背景图路径均已就绪（成功或失败）
    std::vector<std::string> m_preloadPaths; ///< Boot 阶段需等待就绪的分组背景图路径

    // ── 动画参数 ──
    static constexpr float M_DRAW_DURATION = 1.2f;   ///< "M" 路径绘制时长
    static constexpr float I_STEM_DURATION = 0.4f;    ///< "i" 竖线绘制时长
    static constexpr float I_DOT_DURATION = 0.2f;     ///< "i" 点绘制时长
    static constexpr float HITS_SLIDE_DURATION = 0.8f;///< "hits" 滑入时长
    static constexpr float MINIMUM_DURATION = M_DRAW_DURATION + I_STEM_DURATION + I_DOT_DURATION + HITS_SLIDE_DURATION;
};

} // namespace melody_matrix::core

#pragma once

namespace melody_matrix::core {

/// 场景过渡效果：黑屏淡入淡出。
/// 挂载在 StateManager 上，在状态切换时自动播放过渡动画。
/// FadingOut: 当前场景逐渐变黑 → 触发实际状态切换
/// FadingIn: 黑屏逐渐变透明，露出新场景
class SceneTransition {
public:
    /// 开始淡出（当前场景 → 黑屏）
    void startFadeOut(float duration);

    /// 开始淡入（黑屏 → 新场景）
    void startFadeIn(float duration);

    /// 过渡是否正在进行
    bool active() const;

    /// 淡出是否已完成（等待切换状态）
    bool fadeOutComplete() const;

    /// 更新过渡进度
    void update(float dt);

    /// 渲染黑色遮罩（在所有场景渲染之后调用）
    void render();

    /// 获取当前遮罩透明度（0=完全透明, 1=全黑）
    float alpha() const { return m_alpha; }

private:
    float m_alpha = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;

    enum class Phase {
        None,
        FadingOut,   // 场景 → 黑屏（alpha: 0→1）
        FadingIn     // 黑屏 → 场景（alpha: 1→0）
    };
    Phase m_phase = Phase::None;
};

} // namespace melody_matrix::core

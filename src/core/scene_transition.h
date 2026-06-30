/**
 * @file scene_transition.h
 * @brief 场景切换黑屏过渡效果
 *
 * 文件职责：
 *   定义淡出/淡入两阶段过渡的状态机与 ImGui 全屏遮罩渲染接口。
 *
 * 主要依赖：
 *   无外部项目依赖；实现使用 ImGui（scene_transition.cpp）。
 *
 * 在项目中的用法：
 *   由 StateManager 内部持有；外部一般通过 transition() 查询 active/alpha。
 */
#pragma once

namespace melody_matrix::core {

/**
 * @brief 场景过渡效果：全屏黑色遮罩淡入淡出
 *
 * FadingOut：当前场景 alpha 0→1（变黑）→ 触发状态切换
 * FadingIn：黑屏 alpha 1→0 → 露出新场景
 */
class SceneTransition {
public:
    /**
     * @brief 开始淡出（当前场景 → 全黑）
     * @param duration 动画时长（秒）
     */
    void startFadeOut(float duration);

    /**
     * @brief 开始淡入（全黑 → 新场景）
     * @param duration 动画时长（秒）
     */
    void startFadeIn(float duration);

    /**
     * @brief 过渡是否进行中
     * @return true 表示 FadingOut 或 FadingIn
     */
    bool active() const;

    /**
     * @brief 淡出是否已完成
     * @return true 表示可安全执行状态切换
     */
    bool fadeOutComplete() const;

    /**
     * @brief 更新过渡进度
     * @param dt 帧时间（秒）
     */
    void update(float dt);

    /**
     * @brief 渲染黑色全屏遮罩
     *
     * 须在 StateManager::render() 末尾、所有场景绘制之后调用。
     */
    void render();

    /**
     * @brief 当前遮罩不透明度
     * @return 0=完全透明，1=全黑
     */
    float alpha() const { return m_alpha; }

private:
    float m_alpha = 0.0f;      ///< 当前遮罩 alpha
    float m_duration = 0.0f;   ///< 当前阶段总时长
    float m_elapsed = 0.0f;    ///< 当前阶段已用时间

    /** @brief 过渡阶段 */
    enum class Phase {
        None,       ///< 无过渡
        FadingOut,  ///< 场景 → 黑屏（alpha: 0→1）
        FadingIn    ///< 黑屏 → 场景（alpha: 1→0）
    };
    Phase m_phase = Phase::None;  ///< 当前过渡阶段
};

} // namespace melody_matrix::core

/**
 * @file state_manager.h
 * @brief 游戏状态机管理器
 *
 * 文件职责：
 *   注册、持有全部 GameStateBase 实例，驱动状态转换与淡入淡出过渡。
 *
 * 主要依赖：
 *   game_state.h、game_state_base.h、scene_transition.h。
 *
 * 在项目中的用法：
 *   Kernel 持有 StateManager；main 注册各状态后 transitionTo(Boot)；
 *   主循环每帧调用 update/render；各状态通过 update 返回值或 transitionTo 切换。
 */
#pragma once

#include "game_state.h"
#include "game_state_base.h"
#include "scene_transition.h"

#include <memory>
#include <unordered_map>

namespace melody_matrix::core {

/**
 * @brief 管理游戏状态转换
 *
 * 同一时间仅一个活动状态；切换时播放 SceneTransition 黑屏过渡（首次 Boot 除外）。
 * 过渡期间仍更新旧状态以保持动画连续。
 */
class StateManager {
public:
    StateManager() = default;

    /**
     * @brief 注册状态实例
     * @param id 状态枚举 ID
     * @param state 状态对象所有权（unique_ptr）
     */
    void registerState(GameState id, std::unique_ptr<GameStateBase> state);

    /**
     * @brief 请求转换到新状态
     * @param newState 目标状态
     *
     * 若与当前相同则忽略；非首次切换会先淡出再 onExit/onEnter 再淡入。
     */
    void transitionTo(GameState newState);

    /**
     * @brief 获取当前活动状态 ID
     * @return 当前 GameState
     */
    GameState currentState() const { return m_currentState; }

    /**
     * @brief 固定步长更新活动状态
     * @param dt 固定时间步（秒）
     *
     * 处理过渡动画；若状态 update 返回新 ID 则触发 transitionTo。
     */
    void update(float dt);

    /**
     * @brief 渲染活动状态及过渡遮罩
     */
    void render();

    /**
     * @brief 获取场景过渡对象
     * @return SceneTransition 引用，可查询 alpha/active 等
     */
    SceneTransition& transition() { return m_transition; }

    /**
     * @brief 获取指定类型的状态实例（状态间数据传递）
     * @tparam T 具体状态类（如 PlayingState）
     * @param id 状态 ID
     * @return 转换成功返回指针，否则 nullptr
     */
    template <typename T>
    T* getStateAs(GameState id) {
        auto it = m_states.find(id);
        if (it != m_states.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }

private:
    /**
     * @brief 立即执行 onExit/onEnter 并更新渲染标志
     * @param newState 目标状态
     */
    void executeTransition(GameState newState);

    std::unordered_map<GameState, std::unique_ptr<GameStateBase>> m_states;
    GameState m_currentState = GameState::Count; ///< 尚未进入任何状态（确保首次 transitionTo 触发 onEnter）
    GameState m_pendingState = GameState::Count; ///< 过渡完成后待切换的目标

    SceneTransition m_transition;
    bool m_transitioning = false;  ///< 是否正在执行过渡动画

    static constexpr float FADE_DURATION = 0.2f; ///< 淡入/淡出时长（200ms）
};

} // namespace melody_matrix::core

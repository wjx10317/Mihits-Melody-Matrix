#pragma once

#include "game_state.h"
#include "game_state_base.h"
#include "scene_transition.h"

#include <memory>
#include <unordered_map>

namespace melody_matrix::core {

/// 管理游戏状态转换。拥有所有状态实例。
/// 同一时间只有一个状态处于活动状态；状态之间互不依赖。
/// 集成 SceneTransition，在状态切换时自动播放黑屏淡入淡出过渡。
class StateManager {
public:
    StateManager() = default;

    /// 注册状态实例
    void registerState(GameState id, std::unique_ptr<GameStateBase> state);

    /// 转换到新状态（调用 onExit/onEnter）
    void transitionTo(GameState newState);

    /// 获取当前状态标识符
    GameState currentState() const { return m_currentState; }

    /// 固定步长更新 — 委托给活动状态。
    /// 如果状态请求转换，则执行过渡动画后切换状态。
    void update(float dt);

    /// 可变步长渲染 — 委托给活动状态，然后渲染过渡遮罩
    void render();

    /// 获取场景过渡对象（供外部查询过渡状态）
    SceneTransition& transition() { return m_transition; }

    /// 获取转换为特定类型的状态实例（用于状态间数据传递）。
    /// 如果状态未注册或转换失败，返回 nullptr。
    template <typename T>
    T* getStateAs(GameState id) {
        auto it = m_states.find(id);
        if (it != m_states.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }

private:
    void executeTransition(GameState newState);

    std::unordered_map<GameState, std::unique_ptr<GameStateBase>> m_states;
    GameState m_currentState = GameState::Count; // "尚未进入任何状态" — 确保第一次 transitionTo() 触发 onEnter
    GameState m_pendingState = GameState::Count; // 等待过渡完成后切换的目标状态

    SceneTransition m_transition;
    bool m_transitioning = false;  ///< 是否正在执行过渡动画

    static constexpr float FADE_DURATION = 0.2f; ///< 过渡动画时长（200ms）
};

} // namespace melody_matrix::core

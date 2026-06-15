#pragma once

#include "game_state.h"
#include "game_state_base.h"

#include <memory>
#include <unordered_map>

namespace melody_matrix::core {

/// 管理游戏状态转换。拥有所有状态实例。
/// 同一时间只有一个状态处于活动状态；状态之间互不依赖。
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
    /// 如果状态请求转换，则执行转换。
    void update(float dt);

    /// 可变步长渲染 — 委托给活动状态
    void render();

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
    std::unordered_map<GameState, std::unique_ptr<GameStateBase>> m_states;
    GameState m_currentState = GameState::Count; // "尚未进入任何状态" — 确保第一次 transitionTo() 触发 onEnter
};

} // namespace melody_matrix::core

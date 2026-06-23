#include "state_manager.h"
#include "kernel.h"
#include "util/logger.h"

namespace melody_matrix::core {

void StateManager::registerState(GameState id, std::unique_ptr<GameStateBase> state) {
    m_states[id] = std::move(state);
}

void StateManager::transitionTo(GameState newState) {
    if (newState == m_currentState) {
        return;
    }

    // 首次状态切换（BootState）直接执行，无需过渡动画
    if (m_currentState == GameState::Count) {
        executeTransition(newState);
        return;
    }

    // 启动淡出过渡
    if (!m_transitioning) {
        m_pendingState = newState;
        m_transitioning = true;
        m_transition.startFadeOut(FADE_DURATION);

        MM_LOG_INFO("StateManager",
                    std::string("Transition starting: ") + gameStateName(m_currentState) +
                        " -> " + gameStateName(newState));
    }
}

void StateManager::executeTransition(GameState newState) {
    MM_LOG_INFO("StateManager",
                std::string("State transition: ") + gameStateName(m_currentState) +
                    " -> " + gameStateName(newState));

    // Exit current state
    auto it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->onExit();
    }

    // Enter new state
    m_currentState = newState;
    it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->onEnter();
    } else {
        MM_LOG_ERROR("StateManager",
                     std::string("State not registered: ") + gameStateName(newState));
    }

    // ── Update gameplay rendering flag ──
    // Only enable OpenGL gameplay rendering (grid, notes, etc.) during Playing/Paused
    bool isGameplay = (newState == GameState::Playing || newState == GameState::Paused);
    Kernel::instance().renderer().setGameplayRendering(isGameplay);

    // 离开游戏状态时清除背景纹理
    if (!isGameplay) {
        Kernel::instance().renderer().setBackgroundPath("");
    }
}

void StateManager::update(float dt) {
    // 更新过渡动画
    if (m_transitioning) {
        m_transition.update(dt);

        // 淡出完成 → 执行实际状态切换 → 开始淡入
        if (m_transition.fadeOutComplete()) {
            executeTransition(m_pendingState);
            m_pendingState = GameState::Count;
            m_transition.startFadeIn(FADE_DURATION);
        }

        // 淡入完成 → 过渡结束
        if (!m_transition.active()) {
            m_transitioning = false;
        }

        // 过渡期间仍更新当前状态（保持动画运行）
    }

    auto it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        GameState next = it->second->update(dt);
        if (next != m_currentState && next != GameState::Count) {
            transitionTo(next);
        }
    }
}

void StateManager::render() {
    auto it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->render();
    }

    // 渲染过渡遮罩（在所有场景渲染之后）
    m_transition.render();
}

} // namespace melody_matrix::core

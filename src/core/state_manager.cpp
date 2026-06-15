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
}

void StateManager::update(float dt) {
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
}

} // namespace melody_matrix::core

/**
 * @file state_manager.cpp
 * @brief StateManager 实现
 *
 * 文件职责：
 *   实现状态注册、带过渡的切换、update/render 委托及 gameplay 渲染标志同步。
 *
 * 主要依赖：
 *   state_manager.h、kernel.h、util/logger.h。
 *
 * 在项目中的用法：
 *   由 Kernel 主循环每帧驱动，业务代码通过 kernel.stateManager() 访问。
 */
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

    // 首次进入（Boot）直接切换，跳过过渡动画
    if (m_currentState == GameState::Count) {
        executeTransition(newState);
        return;
    }

    // 启动淡出 → 完成后 executeTransition → 淡入
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

    // 退出当前状态
    auto it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->onExit();
    }

    // 进入新状态
    m_currentState = newState;
    it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->onEnter();
    } else {
        MM_LOG_ERROR("StateManager",
                     std::string("State not registered: ") + gameStateName(newState));
    }

    // ── 同步 gameplay OpenGL 渲染开关 ──
    // 仅在 Playing/Paused 时绘制谱面网格与音符
    bool isGameplay = (newState == GameState::Playing || newState == GameState::Paused);
    Kernel::instance().renderer().setGameplayRendering(isGameplay);

    // 离开游戏界面时清除背景纹理，避免菜单仍显示谱面背景
    if (!isGameplay) {
        Kernel::instance().renderer().setBackgroundPath("");
    }
}

void StateManager::update(float dt) {
    // ── 过渡动画更新 ──
    if (m_transitioning) {
        m_transition.update(dt);

        // 淡出完成 → 实际切换状态 → 开始淡入
        if (m_transition.fadeOutComplete()) {
            executeTransition(m_pendingState);
            m_pendingState = GameState::Count;
            m_transition.startFadeIn(FADE_DURATION);
        }

        // 淡入结束 → 过渡流程完成
        if (!m_transition.active()) {
            m_transitioning = false;
        }

        // 过渡期间仍更新当前状态（保持 UI 动画等）
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

    // 过渡黑幕绘制在所有场景内容之上
    m_transition.render();
}

} // namespace melody_matrix::core

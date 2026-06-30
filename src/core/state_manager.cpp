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
#include "state_manager.h"  // StateManager 类声明
#include "kernel.h"           // Kernel::instance().renderer()：同步 gameplay 渲染开关
#include "util/logger.h"      // MM_LOG_INFO / MM_LOG_ERROR：状态切换日志

namespace melody_matrix::core {

void StateManager::registerState(GameState id, std::unique_ptr<GameStateBase> state) {
    m_states[id] = std::move(state);  // 将状态实例存入映射表，id 为键
}

void StateManager::transitionTo(GameState newState) {
    // 目标与当前相同则无需切换
    if (newState == m_currentState) {
        return;
    }

    // 首次进入（Boot）直接切换，跳过过渡动画
    if (m_currentState == GameState::Count) {
        executeTransition(newState);  // 立即 onExit/onEnter，无黑屏过渡
        return;
    }

    // 启动淡出 → 完成后 executeTransition → 淡入
    if (!m_transitioning) {
        m_pendingState = newState;              // 记录淡出完成后要进入的状态
        m_transitioning = true;                 // 标记过渡流程已开始
        m_transition.startFadeOut(FADE_DURATION);  // 启动黑屏淡出动画

        MM_LOG_INFO("StateManager",
                    std::string("Transition starting: ") + gameStateName(m_currentState) +
                        " -> " + gameStateName(newState));
    }
    // 若已在过渡中，忽略新的 transitionTo 请求（防止重入）
}

void StateManager::executeTransition(GameState newState) {
    MM_LOG_INFO("StateManager",
                std::string("State transition: ") + gameStateName(m_currentState) +
                    " -> " + gameStateName(newState));

    // 退出当前状态
    const GameState oldState = m_currentState;
    auto it = m_states.find(m_currentState);  // 查找当前活动状态实例
    if (it != m_states.end()) {
        it->second->onExit();  // 调用旧状态的清理逻辑
    }

    const bool wasGameplay = (oldState == GameState::Playing || oldState == GameState::Paused);
    const bool isGameplay = (newState == GameState::Playing || newState == GameState::Paused);

    // 离开游玩界面时先清谱面背景，再 onEnter — 避免 SongSelect::onEnter 设路径后被此处清掉
    if (wasGameplay && !isGameplay) {
        Kernel::instance().renderer().setBackgroundPath("");
    }

    // 进入新状态
    m_currentState = newState;  // 更新当前状态 ID
    it = m_states.find(m_currentState);  // 查找新状态实例
    if (it != m_states.end()) {
        it->second->onEnter();  // 调用新状态的初始化逻辑
    } else {
        MM_LOG_ERROR("StateManager",
                     std::string("State not registered: ") + gameStateName(newState));
    }

    // ── 同步 gameplay OpenGL 渲染开关 ──
    // 仅在 Playing/Paused 时绘制谱面网格与音符
    Kernel::instance().renderer().setGameplayRendering(isGameplay);
}

void StateManager::update(float dt) {
    // ── 过渡动画更新 ──
    if (m_transitioning) {
        m_transition.update(dt);  // 推进淡出/淡入 alpha

        // 淡出完成 → 实际切换状态 → 开始淡入
        if (m_transition.fadeOutComplete()) {
            executeTransition(m_pendingState);           // 在黑屏全遮时切换 onExit/onEnter
            m_pendingState = GameState::Count;             // 清除待切换目标
            m_transition.startFadeIn(FADE_DURATION);       // 启动淡入，露出新场景
        }

        // 淡入结束 → 过渡流程完成
        if (!m_transition.active()) {
            m_transitioning = false;  // 过渡动画全部结束
        }

        // 过渡期间仍更新当前状态（保持 UI 动画等）
    }

    auto it = m_states.find(m_currentState);  // 查找当前活动状态
    if (it != m_states.end()) {
        GameState next = it->second->update(dt);  // 固定步长逻辑更新，可能返回目标状态
        // 状态主动请求切换且目标有效
        if (next != m_currentState && next != GameState::Count) {
            transitionTo(next);  // 发起带过渡的状态切换
        }
    }
}

void StateManager::render() {
    auto it = m_states.find(m_currentState);
    if (it != m_states.end()) {
        it->second->render();  // 绘制当前状态的 ImGui / 内容
    }

    // 过渡黑幕绘制在所有场景内容之上
    m_transition.render();  // 全屏黑色遮罩覆盖最上层
}

} // namespace melody_matrix::core

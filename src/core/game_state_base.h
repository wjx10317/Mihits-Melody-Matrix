/**
 * @file game_state_base.h
 * @brief 游戏状态抽象基类
 *
 * 文件职责：
 *   定义所有具体状态必须实现的 onEnter/onExit/update/render 接口。
 *
 * 主要依赖：
 *   game_state.h。
 *
 * 在项目中的用法：
 *   各 states/* 继承 GameStateBase；StateManager 通过基类指针统一调度。
 */
#pragma once

#include "game_state.h"  // GameState 枚举：update() 返回值及状态标识

namespace melody_matrix::core {

/**
 * @brief 所有游戏状态的抽象基类
 *
 * 每个状态独立拥有更新/渲染逻辑；update() 返回值可请求状态转换。
 * 状态之间通过 StateManager::getStateAs<T>() 传递数据，不直接互相引用。
 */
class GameStateBase {
public:
    virtual ~GameStateBase() = default;  // 多态析构，确保派生类资源正确释放

    /**
     * @brief 状态变为活动态时调用
     *
     * 用于初始化 UI、加载资源、重置局部变量等。
     */
    virtual void onEnter() = 0;  // 进入状态时的一次性初始化

    /**
     * @brief 状态被替换离开时调用
     *
     * 用于释放临时资源、停止预览音频等。
     */
    virtual void onExit() = 0;  // 离开状态时的清理

    /**
     * @brief 固定步长逻辑更新（240 Hz）
     * @param dt 固定时间步（秒），通常 1/240
     * @return 请求切换到的状态；GameState::Count 表示保持当前状态
     */
    virtual GameState update(float dt) = 0;  // 每固定步长 tick 一次逻辑更新

    /**
     * @brief 可变步长渲染（显示器刷新率）
     *
     * 在 OpenGL 清屏与 ImGui newFrame 之间由 StateManager 调用。
     */
    virtual void render() = 0;  // 每渲染帧绘制当前状态 UI/内容
};

} // namespace melody_matrix::core

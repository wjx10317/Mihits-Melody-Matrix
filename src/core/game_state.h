/**
 * @file game_state.h
 * @brief 游戏状态枚举与名称工具
 *
 * 文件职责：
 *   定义 GameState 枚举及 gameStateName() 日志辅助函数。
 *
 * 主要依赖：
 *   标准库 <string>。
 *
 * 在项目中的用法：
 *   StateManager、各 State 实现及 Kernel 通过 GameState 标识当前/目标状态；
 *   gameStateName() 用于日志输出可读状态名。
 */
#pragma once

#include <string>

namespace melody_matrix::core {

/**
 * @brief 游戏状态标识符
 *
 * Count 为哨兵值，表示“保持当前状态”或“尚未进入任何状态”。
 */
enum class GameState {
    Boot,       ///< 启动加载
    MainMenu,   ///< 主菜单
    SongSelect, ///< 选曲
    Playing,    ///< 游玩中
    Paused,     ///< 暂停
    Result,     ///< 结算
    Count       ///< 哨兵：无效/保持不变
};

/**
 * @brief 获取状态的英文名称字符串（用于日志）
 * @param state 状态枚举
 * @return 静态 C 字符串，未知状态返回 "Unknown"
 */
inline const char* gameStateName(GameState state) {
    switch (state) {
    case GameState::Boot:       return "Boot";
    case GameState::MainMenu:   return "MainMenu";
    case GameState::SongSelect: return "SongSelect";
    case GameState::Playing:    return "Playing";
    case GameState::Paused:     return "Paused";
    case GameState::Result:     return "Result";
    default:                    return "Unknown";
    }
}

} // namespace melody_matrix::core

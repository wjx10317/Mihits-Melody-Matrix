#pragma once

#include <string>

namespace melody_matrix::core {

/// 游戏状态标识符
enum class GameState {
    Boot,
    MainMenu,
    SongSelect,
    Playing,
    Paused,
    Result,
    Count  // 哨兵值
};

/// 获取状态名称字符串（用于日志）
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

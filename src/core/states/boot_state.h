#pragma once

#include "core/game_state_base.h"

namespace melody_matrix::core {

/// 启动状态：应用启动时进入的第一个状态。
/// 初始化窗口、GL 上下文，扫描铺面并预加载图片资源，然后转换到主菜单。
class BootState : public GameStateBase {
public:
    BootState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

private:
    bool m_ready = false;
    bool m_preloaded = false;  ///< 铺面图片是否已预加载完成
};

} // namespace melody_matrix::core

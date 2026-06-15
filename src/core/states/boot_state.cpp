#include "boot_state.h"
#include "core/kernel.h"
#include "core/states/song_select_state.h"
#include "util/logger.h"

namespace melody_matrix::core {

void BootState::onEnter() {
    MM_LOG_INFO("Boot", "Entering Boot state");
    m_ready = true;
}

void BootState::onExit() {
    MM_LOG_INFO("Boot", "Exiting Boot state");
}

GameState BootState::update(float dt) {
    if (m_ready && !m_preloaded) {
        // 扫描铺面并预加载图片到全局纹理缓存
        auto* songSelect = Kernel::instance().stateManager().getStateAs<SongSelectState>(GameState::SongSelect);
        if (songSelect) {
            songSelect->scanAndPreload();
        }
        m_preloaded = true;
    }

    if (m_preloaded) {
        return GameState::MainMenu;
    }
    return GameState::Count; // Stay in current state
}

void BootState::render() {
    // Nothing to render during boot — the clear color is set by Kernel
    // 未来可在此绘制加载进度条、Logo等元素
}

} // namespace melody_matrix::core

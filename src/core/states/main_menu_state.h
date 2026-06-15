#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"
#include "util/result.h"

#include <string>
#include <unordered_set>

namespace melody_matrix::core {

/// 主菜单状态：带有开始/设置/导入/退出选项的标题屏幕。
/// 设置作为左侧滑动侧边栏覆盖层渲染。
class MainMenuState : public GameStateBase {
public:
    MainMenuState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

    /// 获取已导入铺面的 SHA256 哈希集合（供导入去重使用）
    std::unordered_set<std::string>& importedHashes() { return m_importedHashes; }
    const std::unordered_set<std::string>& importedHashes() const { return m_importedHashes; }

private:
    void renderImGuiPanel();
    void renderSettingsOverlay();
    void renderSettingsSidebar();
    void loadBackgroundTexture();

    // ── 导入功能 ──
    void importOszFile(const std::string& oszPath);
    util::Result<void> validateAndImportOsz(const std::string& oszPath);
    util::Result<void> importSingleOsu(const std::string& osuPath, const std::string& extractRoot);

    GameState m_nextState = GameState::Count;
    bool m_settingsOpen = false;
    float m_settingsSlideProgress = 0.0f; // 0.0 = 关闭, 1.0 = 完全打开

    // Resolution selection
    int m_selectedResolutionIndex = 1; // default 1920x1080
    bool m_fullscreen = false;

    // Deferred resolution change (applied in update(), not during ImGui render)
    int m_pendingResolutionW = 0;
    int m_pendingResolutionH = 0;
    bool m_pendingFullscreen = false;
    bool m_hasPendingFullscreen = false;

    // Background texture
    renderer::Texture2D m_bgTexture;

    // ── 导入状态 ──
    std::unordered_set<std::string> m_importedHashes; ///< 已导入 .osu 的 SHA256 哈希集合
    bool m_importInProgress = false;                   ///< 是否正在导入
    std::string m_importMessage;                       ///< 导入结果消息（成功/失败）
    float m_importMessageTimer = 0.0f;                 ///< 消息显示倒计时
    bool m_importSuccess = false;                       ///< 上次导入是否成功
};

} // namespace melody_matrix::core

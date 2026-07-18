// ============================================================
// main_menu_state.h — 主菜单状态
//
// 标题屏幕：提供开始游戏、设置、导入 .osz 铺面与退出选项。
// 设置以左侧滑入侧边栏覆盖层形式呈现，支持分辨率与全屏切换。
// ============================================================
#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"
#include "util/result.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace melody_matrix::core {

/// 主菜单状态：带有开始/设置/导入/退出选项的标题屏幕。
/// 设置作为左侧滑动侧边栏覆盖层渲染。
class MainMenuState : public GameStateBase {
public:
    MainMenuState() = default;

    /// 进入主菜单：加载背景、分辨率设置与已导入哈希
    void onEnter() override;
    /// 退出主菜单
    void onExit() override;
    /// 每帧更新：延迟分辨率变更、侧边栏动画、导入消息计时
    GameState update(float dt) override;
    /// 渲染背景、主面板与设置侧边栏
    void render() override;

    /// 获取已导入铺面的 SHA256 哈希集合（供导入去重使用）
    std::unordered_set<std::string>& importedHashes() { return m_importedHashes; }
    const std::unordered_set<std::string>& importedHashes() const { return m_importedHashes; }

private:
    /// 渲染主菜单 ImGui 面板（标题、按钮、导入）
    void renderImGuiPanel();
    /// 渲染设置侧边栏外的半透明遮罩
    void renderSettingsOverlay();
    /// 渲染左侧滑入的设置侧边栏（分辨率、全屏等）
    void renderSettingsSidebar();
    /// 尝试从多个路径加载菜单背景纹理
    void loadBackgroundTexture();

    // ── 导入功能（osz → 临时解压 → 逐 osu → mma + 资源复制）──
    /// 批量导入多个 .osz，汇总成功/跳过/失败消息
    void importOszFiles(const std::vector<std::string>& oszPaths);
    /// 校验扩展名、解压 zip、遍历内部 .osu 调用 importSingleOsu；不写 UI 文案
    /// outImported/outSkipped 可选，写入本包新导入/已存在跳过数量
    util::Result<void> validateAndImportOsz(const std::string& oszPath,
                                            int* outImported = nullptr,
                                            int* outSkipped = nullptr);
    /// 单 .osu 解析校验、SHA256 去重、写 .mma、复制音频与 background.*
    util::Result<void> importSingleOsu(const std::string& osuPath, const std::string& extractRoot);

    GameState m_nextState = GameState::Count;
    bool m_settingsOpen = false;
    float m_settingsSlideProgress = 0.0f; ///< 0.0 = 关闭, 1.0 = 完全打开

    // ── 分辨率选择 ──
    int m_selectedResolutionIndex = 1; ///< 默认 1920x1080
    bool m_fullscreen = false;

    // 延迟分辨率变更（在 update() 中应用，避免 ImGui 渲染期间改窗口）
    int m_pendingResolutionW = 0;
    int m_pendingResolutionH = 0;
    bool m_pendingFullscreen = false;
    bool m_hasPendingFullscreen = false;

    // ── 背景纹理 ──
    renderer::Texture2D m_bgTexture;

    // ── 导入状态 ──
    std::unordered_set<std::string> m_importedHashes; ///< 已导入 .osu 的 SHA256 哈希集合
    std::string m_importMessage;                       ///< 导入结果消息（成功/失败）
    float m_importMessageTimer = 0.0f;                 ///< 消息显示倒计时
    bool m_importSuccess = false;                       ///< 上次导入是否成功
};

} // namespace melody_matrix::core

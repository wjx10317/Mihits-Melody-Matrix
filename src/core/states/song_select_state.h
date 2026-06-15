#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"
#include "renderer/texture_cache.h"
#include "audio/audio_engine.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace melody_matrix::core {

/// 歌曲选择状态：左右分割布局，左侧为排行榜+操作区，右侧为铺面列表。
/// 铺面按歌曲分组（BeatmapGroup），每组包含多个难度（set），只有 set 可游玩。
class SongSelectState : public GameStateBase {
public:
    SongSelectState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

    /// Set the beatmap directory to scan
    void setBeatmapDir(const std::string& dir) { m_beatmapDir = dir; }

    /// Get the currently selected beatmap path
    const std::string& selectedBeatmap() const { return m_selectedBeatmap; }

    /// 当弹窗打开时，ESC 应由本状态消费（关闭弹窗），而非返回上级状态
    bool shouldConsumeEscape() const { return m_modPopupOpen; }

    /// 标记需要重新扫描铺面（导入新铺面后调用）
    void markNeedsRescan() { m_scanDone = false; }

    /// 扫描铺面并预加载图片到全局纹理缓存（供 BootState 调用）
    void scanAndPreload();

    /// 获取所有分组的图片路径列表（与 m_groups 一一对应）
    std::vector<std::string> getGroupImagePaths() const;

    /// 尝试播放当前选中 set 的预览音频
    void tryPlayPreview();

private:
    void scanBeatmaps();
    void renderImGuiPanel();
    void renderLeftPanel(float panelWidth, float panelHeight);
    void renderRightPanel(float panelX, float panelWidth, float panelHeight);
    void renderModPopup();
    void loadAvatarTexture();
    void tryLoadGroupImage(int groupIndex);
    void unloadUnusedImages();

    /// 获取当前选中的 set 条目（可能为 nullptr）
    struct BeatmapEntry;
    const BeatmapEntry* getSelectedSet() const;

    // ── 排行榜模式 ──
    enum class LeaderboardMode : int {
        Local = 0,
        Friend = 1
    };

    // ── Mod 项 ──
    struct ModItem {
        std::string name;       ///< 显示名称
        std::string id;         ///< 内部标识
        bool active = false;    ///< 是否启用
        bool implemented = false; ///< 是否已实装
    };

    // ── 铺面条目 ──
    struct BeatmapEntry {
        std::string filePath;
        std::string title;
        std::string artist;
        std::string creator;     ///< 铺面作者
        std::string version;     ///< 难度名称
        float difficulty = 0.0f; ///< 难度值（星级）
        float ar = 5.0f;        ///< 接近速率
        float od = 5.0f;        ///< 总体难度
        float hp = 5.0f;        ///< HP消耗速率
        int noteCount = 0;       ///< 音符总数
        float duration = 0.0f;   ///< 时长（秒）
        std::string imagePath;   ///< 背景图片路径（相对于铺面目录或绝对路径）
        std::string audioFilePath; ///< 音频文件绝对路径
        int64_t previewTime = 0; ///< 预览起始时间（毫秒），0=从头
    };

    // ── 铺面分组（歌曲级，包含多个难度 set）──
    struct BeatmapGroup {
        std::string title;                    ///< 歌曲标题
        std::string artist;                   ///< 艺术家
        std::string imagePath;                ///< 背景图片路径（组内共享）
        std::vector<BeatmapEntry> sets;       ///< 子难度列表
    };

    // ── 铺面数据 ──
    std::vector<BeatmapGroup> m_groups;
    std::string m_beatmapDir = "assets/beatmaps";
    std::string m_selectedBeatmap;
    int m_selectedGroup = -1;          ///< 当前选中的组（同时也是展开的组）
    int m_selectedSet = -1;            ///< 选中的 set 索引（组内，-1=未选中）
    bool m_scanDone = false;
    GameState m_nextState = GameState::Count;
    bool m_scrollToSelected = false;   ///< 是否需要滚动到选中项

    // ── 排行榜 ──
    LeaderboardMode m_leaderboardMode = LeaderboardMode::Local;

    // ── Mod 系统 ──
    std::vector<ModItem> m_mods;      ///< 可用 Mod 列表
    bool m_modPopupOpen = false;

    // ── 图片/纹理 ──
    renderer::Texture2D m_avatarTexture;           ///< 用户头像纹理
    int m_bgImageGroup = -1;          ///< 当前全屏背景图对应的组索引

    // ── 音频（选歌预览）──
    audio::AudioEngine m_audio;           ///< 预览音频引擎（独立于 PlayingState 的游戏音频）
    std::string m_lastPreviewAudioPath;  ///< 上次触发预览的音频路径（同路径不重启）

       // ── 响应式布局 ──
    struct Layout {
        float W, H;
        float scale;              ///< 相对1080p的缩放因子 (screenH / 1080)
        float leftWidth;
        float rightWidth;
        float topInfoH;
        float bottomBarH;
        float groupHeaderH;
        float setItemH;
        float groupSpacing;
        float setIndent;
        float listIndent;
        float rightPadding;
        float maskArcHeight;      ///< 遮罩底部弧线下凹高度
    };

    void computeLayout(float screenW, float screenH);
    Layout m_ly{};

    static constexpr float LEFT_RATIO = 0.38f;
};

} // namespace melody_matrix::core

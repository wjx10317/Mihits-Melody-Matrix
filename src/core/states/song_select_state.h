// ============================================================
// song_select_state.h — 选歌状态
//
// 左右分割布局：左侧为排行榜与操作区，右侧为按歌曲分组的
// 铺面列表。支持 Mod 选择、预览音频、删除铺面与开始游戏。
// ============================================================
#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"
#include "renderer/texture_cache.h"
#include "audio/audio_engine.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

namespace melody_matrix::core {

/// 歌曲选择状态：左右分割布局，左侧为排行榜+操作区，右侧为铺面列表。
/// 铺面按歌曲分组（BeatmapGroup），每组包含多个难度（set），只有 set 可游玩。
class SongSelectState : public GameStateBase {
public:
    SongSelectState() = default;

    /// 进入选歌：初始化音频、扫描铺面、加载头像与 Mod 列表
    void onEnter() override;
    /// 退出选歌：停止预览音频、恢复默认背景
    void onExit() override;
    /// 每帧更新：预览音频、背景切换、开始游戏过渡
    GameState update(float dt) override;
    /// 渲染选歌面板、Mod 弹窗与顶部遮罩
    void render() override;

    /// 设置铺面扫描目录
    void setBeatmapDir(const std::string& dir) { m_beatmapDir = dir; }

    /// 获取当前选中的铺面文件路径
    const std::string& selectedBeatmap() const { return m_selectedBeatmap; }

    /// 当弹窗打开时，ESC 应由本状态消费（关闭弹窗），而非返回上级状态
    bool shouldConsumeEscape() const { return m_modPopupOpen; }

    /// 标记需要全量重新扫描（外部改动了 beatmaps 目录等少数场景）
    void markNeedsRescan() { m_scanDone = false; }

    /// Import 写入 .mma 后增量合并到列表，并 requestLoad 背景图（无需 scanBeatmaps）
    void registerImportedMma(const std::string& mmaPath);

    /// 扫描铺面并预加载图片到全局纹理缓存（供 BootState 调用）
    void scanAndPreload();

    /// 扫描铺面目录，构建分组列表（仅文件 I/O + 解析，无需 GL 上下文，可安全在后台线程调用）
    void scanBeatmaps();

    /// 获取所有分组的图片路径列表（与 m_groups 一一对应）
    std::vector<std::string> getGroupImagePaths() const;

    /// 尝试播放当前选中 set 的预览音频
    void tryPlayPreview();

private:
    /// 渲染选歌主 ImGui 面板（左右分割）
    void renderImGuiPanel();
    /// 渲染左侧面板：用户信息、排行榜、Mod 按钮
    void renderLeftPanel(float panelWidth, float panelHeight);
    /// 渲染右侧铺面列表（分组折叠 + 难度条目）
    void renderRightPanel(float panelX, float panelWidth, float panelHeight);
    /// 渲染 Mod 选择弹窗
    void renderModPopup();
    /// 加载用户头像纹理
    void loadAvatarTexture();
    /// 按需加载指定分组的背景图到全局纹理缓存
    void tryLoadGroupImage(int groupIndex);
    /// 同步全屏背景与 Renderer（选中组变化或删除后调用）
    void syncSelectionBackground();
    /// 卸载已删铺面的纹理缓存并释放导入哈希（允许重新导入）
    void releaseDeletedBeatmapAssets(const std::vector<std::string>& imagePaths,
                                     const std::vector<std::string>& sourceHashes);
    /// 卸载不再使用的分组背景纹理
    void unloadUnusedImages();
    /// 删除整组铺面（文件 + 内存列表）
    void deleteBeatmapGroup(int groupIndex);
    /// 删除组内单个难度 set
    void deleteBeatmapSet(int groupIndex, int setIndex);
    /// 删除后修正选中索引
    void fixSelectionAfterDelete();
    /// 在 render 末尾执行延迟删除（避免 ImGui 迭代中修改列表）
    void applyPendingDelete();

    /// 待执行的删除操作（render 末尾统一应用，支持同帧多条）
    struct PendingDeleteAction {
        int groupIndex = -1;
        int setIndex = -1;  ///< -1 表示删除整组
    };
    std::vector<PendingDeleteAction> m_pendingDeletes;

    struct BeatmapEntry;

    // ── 排行榜模式 ──
    enum class LeaderboardMode : int {
        Local = 0,   ///< 本地排行
        Friend = 1   ///< 好友排行（预留）
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

    struct BeatmapGroup {
        std::string title;                    ///< 歌曲标题
        std::string artist;                   ///< 艺术家
        std::string imagePath;                ///< 背景图片路径（组内共享）
        std::vector<BeatmapEntry> sets;       ///< 子难度列表
    };

    /// 从单个 .mma/.osu 路径解析为 BeatmapEntry（scan / import 共用）
    std::optional<BeatmapEntry> parseBeatmapEntry(const std::string& filePath);

    /// 将条目合并进 m_groups（按 title+artist 分组，组内按 difficulty 排序）
    void mergeBeatmapEntry(BeatmapEntry entry);

    /// 获取当前选中的 set 条目（可能为 nullptr）
    const BeatmapEntry* getSelectedSet() const;

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
        float scale;              ///< 相对 1080p 的缩放因子 (screenH / 1080)
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

    /// 根据屏幕尺寸计算响应式布局参数
    void computeLayout(float screenW, float screenH);
    Layout m_ly{};

    static constexpr float LEFT_RATIO = 0.38f;
};

} // namespace melody_matrix::core

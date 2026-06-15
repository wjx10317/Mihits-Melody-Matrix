#!/usr/bin/env python3
"""Complete header generator - writes the full song_select_state.h"""
import pathlib

out = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.h")

text = """\
#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace melody_matrix::core {

class SongSelectState : public GameStateBase {
public:
    SongSelectState() = default;

    void onEnter() override;
    void onExit() override;
    GameState update(float dt) override;
    void render() override;

    void setBeatmapDir(const std::string& dir) { m_beatmapDir = dir; }
    const std::string& selectedBeatmap() const { return m_selectedBeatmap; }
    bool shouldConsumeEscape() const { return m_modPopupOpen; }
    void markNeedsRescan() { m_scanDone = false; }

private:
    void scanBeatmaps();
    void renderImGuiPanel();
    void renderLeftPanel(float panelWidth, float panelHeight);
    void renderRightPanel(float panelX, float panelWidth, float panelHeight);
    void renderModPopup();
    void loadAvatarTexture();
    void tryLoadGroupImage(int groupIndex);
    void unloadUnusedImages();

    struct BeatmapEntry;
    const BeatmapEntry* getSelectedSet() const;

    // -- Leaderboard mode --
    enum class LeaderboardMode : int { Local = 0, Friend = 1 };

    // -- Group/Filter mode --
    enum class GroupMode : int {
        ByTitle = 0,
        ByArtist = 1,
        ByDifficulty = 2,
        ByRecent = 3,
        ByFavorite = 4
    };

    // -- Sort mode --
    enum class SortMode : int {
        ByTitle = 0, ByArtist = 1, ByDifficulty = 2,
        ByDateAdded = 3, ByLength = 4, ByBPM = 5
    };

    // -- Mod item --
    struct ModItem {
        std::string name;
        std::string id;
        bool active = false;
        bool implemented = false;
    };

    // -- Beatmap entry --
    struct BeatmapEntry {
        std::string filePath;
        std::string title;
        std::string artist;
        std::string creator;
        std::string version;
        float difficulty = 0.0f;
        float ar = 5.0f;
        float od = 5.0f;
        float hp = 5.0f;
        float cs = 4.0f;
        int noteCount = 0;
        float duration = 0.0f;
        float bpm = 0.0f;
        std::string imagePath;
    };

    // -- Beatmap group --
    struct BeatmapGroup {
        std::string title;
        std::string artist;
        std::string imagePath;
        std::vector<BeatmapEntry> sets;
    };

    // -- Data --
    std::vector<BeatmapGroup> m_groups;
    std::string m_beatmapDir = "assets/beatmaps";
    std::string m_selectedBeatmap;
    int m_selectedGroup = -1;
    int m_selectedSet = -1;
    bool m_scanDone = false;
    GameState m_nextState = GameState::Count;
    bool m_scrollToSelected = false;

    // -- Leaderboard --
    LeaderboardMode m_leaderboardMode = LeaderboardMode::Local;

    // -- Filter/Group --
    GroupMode m_groupMode = GroupMode::ByTitle;
    SortMode m_sortMode = SortMode::ByTitle;
    bool m_filterFavorite = false;
    bool m_filterRecent = false;
    float m_minDifficulty = 0.0f;
    float m_maxDifficulty = 10.0f;
    bool m_difficultyFilterActive = false;
    std::string m_searchQuery;
    bool m_searchFocused = false;

    // -- Mod system --
    std::vector<ModItem> m_mods;
    bool m_modPopupOpen = false;

    // -- Textures --
    renderer::Texture2D m_avatarTexture;
    std::unordered_map<int, renderer::Texture2D> m_groupImages;
    int m_bgImageGroup = -1;

    // -- Responsive layout --
    struct Layout {
        float W, H;
        float leftWidth;
        float rightWidth;
        float topInfoH;
        float songDetailH;
        float filterBarH;
        float searchBarH;
        float bottomBarH;
        float groupHeaderH;
        float setItemH;
        float groupSpacing;
        float setIndent;
        float listIndent;
        float rightPadding;
    };

    void computeLayout(float screenW, float screenH);
    Layout m_ly{};

    static constexpr float LEFT_RATIO = 0.38f;
};

} // namespace melody_matrix::core
"""

out.write_text(text, encoding="utf-8")
print("Header generated successfully")

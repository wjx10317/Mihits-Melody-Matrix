#!/usr/bin/env python3
import pathlib
out = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.h")
L = []
L.append("\n")
L.append("class SongSelectState : public GameStateBase {\n")
L.append("public:\n")
L.append("    SongSelectState() = default;\n")
L.append("\n")
L.append("    void onEnter() override;\n")
L.append("    void onExit() override;\n")
L.append("    GameState update(float dt) override;\n")
L.append("    void render() override;\n")
L.append("\n")
L.append("    void setBeatmapDir(const std::string& dir) { m_beatmapDir = dir; }\n")
L.append("    const std::string& selectedBeatmap() const { return m_selectedBeatmap; }\n")
L.append("    bool shouldConsumeEscape() const { return m_modPopupOpen; }\n")
L.append("    void markNeedsRescan() { m_scanDone
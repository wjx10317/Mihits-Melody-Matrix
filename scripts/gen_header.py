#!/usr/bin/env python3
"""Generate the full song_select_state.h"""
import pathlib

out = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.h")

parts = []
parts.append("#pragma once")
parts.append("")
parts.append('#include "core/game_state_base.h"')
parts.append('#include "renderer/texture.h"')
parts.append("")
parts.append("#include <vector>")
parts.append("#include <string>")
parts.append("#include <unordered_map>")
parts.append("#include <memory>")
parts.append("")
parts.append("namespace melody_matrix::core {")
parts.append("")
parts.append("class SongSelectState : public GameStateBase {")
parts.append("public:")
parts.append("    SongSelectState() = default;")
parts.append("")
parts.append("    void onEnter() override;")

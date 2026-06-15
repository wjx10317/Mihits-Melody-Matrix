#!/usr/bin/env python3
import pathlib
out = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.h")
L = []
L.append("#pragma once\n")
L.append("\n")
L.append('#include "core/game_state_base.h"\n')
L.append('#include "renderer/texture.h"\n')
L.append("\n")
L.append("#include <vector>\n")
L.append("#include <string>\n")
L.append("#include <unordered_map>\n")
L.append("#include <memory>\n")
L.append("\n")
L.append("namespace melody_matrix::core {\n")
out.write_text("".join(L), encoding="utf-8")
print("part1 done")
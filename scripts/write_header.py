#!/usr/bin/env python3
"""Generate song_select_state.h with the new filter bar design."""
import pathlib

p = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.h")
p.write_text(content, encoding='utf-8')

content = '''#pragma once

#include "core/game_state_base.h"
#include "renderer/texture.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace melody_matrix::core {

/// \\u6b4c\\u66f2\\u9009\\u62e9\\u72b6\\u6001\\uff1a\\u5de6\\u53f3\\u5206\\u5272\\u5e03\\u5c40\\uff0c\\u5de6\\u4fa7\\u4e3a\\u8be6\\u
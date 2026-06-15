#!/usr/bin/env python3
"""Patch song_select_state.cpp: replace flat mask with arc-bottom mask"""
import pathlib

p = pathlib.Path(r"d:\colin\Melody-Matrix\src\core\states\song_select_state.cpp")
text = p.read_text(encoding='utf-8')

old = """    // ── 右侧面板顶部遮罩（遮挡滚入的列表项）──
    // 从左面板右缘 → 列表起始位置，与左侧实心遮罩视觉衔接
    if (!m_modPopupOpen) {
        ImDrawList* fgDl = ImGui::GetForegroundDrawList();
        fgDl->AddRectFilled(
            ImVec2(m_ly.leftWidth, 0),
            ImVec2(m_ly.leftWidth + m_ly.listIndent + m_ly.rightPadding * 2, m_ly.topInfoH),
            IM_COL32(13, 13, 23, 255)
        );
    }"""


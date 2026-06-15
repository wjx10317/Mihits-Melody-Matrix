#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"

namespace melody_matrix::ui {

void Theme::apply() {
    MM_LOG_INFO("Theme", "Applying deep neon theme");

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ── Rounded corners ──
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    // ── Spacing ──
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    // ── Color palette ──
    // Background shades (#1a1a2e family)
    ImVec4 bgMain   = ImVec4(BG_R, BG_G, BG_B, 1.0f);           // #1a1a2e
    ImVec4 bgLight  = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);        // #242438
    ImVec4 bgLighter= ImVec4(0.18f, 0.18f, 0.28f, 1.0f);        // #2e2e47
    ImVec4 bgPanel  = ImVec4(BG_R, BG_G, BG_B, 0.94f);

    // Accent colors
    ImVec4 cyan     = ImVec4(CYAN_R, CYAN_G, CYAN_B, 1.0f);     // #00fff5
    ImVec4 purple   = ImVec4(PURP_R, PURP_G, PURP_B, 1.0f);     // #b300ff
    ImVec4 pink     = ImVec4(PINK_R, PINK_G, PINK_B, 1.0f);     // #ff006e
    ImVec4 cyanDim  = ImVec4(CYAN_R, CYAN_G, CYAN_B, 0.6f);
    ImVec4 purpleDim= ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);

    // Text
    ImVec4 textMain = ImVec4(0.95f, 0.95f, 0.98f, 1.0f);        // near white
    ImVec4 textDim  = ImVec4(0.55f, 0.55f, 0.65f, 1.0f);        // muted

    // ── Apply colors ──
    colors[ImGuiCol_Text]                  = textMain;
    colors[ImGuiCol_TextDisabled]          = textDim;
    colors[ImGuiCol_WindowBg]              = bgPanel;
    colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]               = bgPanel;
    colors[ImGuiCol_Border]                = ImVec4(0.2f, 0.2f, 0.35f, 0.5f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = bgLighter;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.26f, 0.42f, 1.0f);
    colors[ImGuiCol_TitleBg]               = bgMain;
    colors[ImGuiCol_TitleBgActive]         = bgLight;
    colors[ImGuiCol_TitleBgCollapsed]      = bgMain;
    colors[ImGuiCol_MenuBarBg]             = bgLight;
    colors[ImGuiCol_ScrollbarBg]           = bgMain;
    colors[ImGuiCol_ScrollbarGrab]         = purpleDim;
    colors[ImGuiCol_ScrollbarGrabHovered]  = purple;
    colors[ImGuiCol_ScrollbarGrabActive]   = pink;
    colors[ImGuiCol_CheckMark]             = cyan;
    colors[ImGuiCol_SliderGrab]            = cyanDim;
    colors[ImGuiCol_SliderGrabActive]      = cyan;
    colors[ImGuiCol_Button]                = ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(PURP_R, PURP_G, PURP_B, 0.65f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(PINK_R, PINK_G, PINK_B, 0.8f);
    colors[ImGuiCol_Header]                = ImVec4(PURP_R, PURP_G, PURP_B, 0.3f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(PURP_R, PURP_G, PURP_B, 0.5f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(PURP_R, PURP_G, PURP_B, 0.7f);
    colors[ImGuiCol_Separator]             = ImVec4(0.2f, 0.2f, 0.35f, 0.5f);
    colors[ImGuiCol_SeparatorHovered]      = cyanDim;
    colors[ImGuiCol_SeparatorActive]       = cyan;
    colors[ImGuiCol_ResizeGrip]            = purpleDim;
    colors[ImGuiCol_ResizeGripHovered]     = purple;
    colors[ImGuiCol_ResizeGripActive]      = pink;
    colors[ImGuiCol_Tab]                   = bgLighter;
    colors[ImGuiCol_TabHovered]            = ImVec4(PURP_R, PURP_G, PURP_B, 0.5f);
    colors[ImGuiCol_TabActive]             = ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);
    colors[ImGuiCol_TabUnfocused]          = bgLight;
    colors[ImGuiCol_TabUnfocusedActive]    = bgLighter;
    colors[ImGuiCol_PlotLines]             = cyanDim;
    colors[ImGuiCol_PlotLinesHovered]      = cyan;
    colors[ImGuiCol_PlotHistogram]         = purple;
    colors[ImGuiCol_PlotHistogramHovered]  = pink;
    colors[ImGuiCol_TableHeaderBg]         = bgLighter;
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.2f, 0.2f, 0.35f, 0.6f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.2f, 0.2f, 0.35f, 0.3f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.02f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(CYAN_R, CYAN_G, CYAN_B, 0.2f);
    colors[ImGuiCol_DragDropTarget]        = pink;
    colors[ImGuiCol_NavHighlight]          = cyan;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.6f);

    MM_LOG_INFO("Theme", "Deep neon theme applied");
}

} // namespace melody_matrix::ui

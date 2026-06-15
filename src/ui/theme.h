#pragma once

namespace melody_matrix::ui {

/// ImGui 的深霓虹主题。
/// 背景: #1a1a2e（深海军蓝）
/// 强调色: #00fff5（青色）, #b300ff（紫色）, #ff006e（热粉色）
/// 统一的圆角和一致的间距。
class Theme {
public:
    /// 将主题应用到当前 ImGui 上下文。
    /// 必须在 ImGui::CreateContext() 之后且第一帧之前调用。
    static void apply();

    // ── 主题颜色常量 ──
    static constexpr float BG_R = 0.102f, BG_G = 0.102f, BG_B = 0.180f;  // #1a1a2e
    static constexpr float CYAN_R = 0.0f,  CYAN_G = 1.0f,  CYAN_B = 0.96f; // #00fff5
    static constexpr float PURP_R = 0.702f,PURP_G = 0.0f,  PURP_B = 1.0f;  // #b300ff
    static constexpr float PINK_R = 1.0f,  PINK_G = 0.0f,  PINK_B = 0.431f; // #ff006e

    // ── UI 字符串 ──
    static constexpr const char* FONT_PATH = "assets/fonts/NotoSansSC-Regular.ttf";
    static constexpr float FONT_SIZE = 18.0f;
};

} // namespace melody_matrix::ui

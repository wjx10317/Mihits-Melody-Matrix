// ============================================================
// theme.h — ImGui 深霓虹主题
//
// 定义全局 UI 配色（深海军蓝背景 + 青/紫/粉强调色）与字体路径。
// 通过 Theme::apply() 一次性应用到 ImGui 上下文。
// ============================================================
#pragma once

namespace melody_matrix::ui {

/// ImGui 深霓虹主题配置
/// 背景: #1a1a2e（深海军蓝）
/// 强调色: #00fff5（青色）, #b300ff（紫色）, #ff006e（热粉色）
class Theme {
public:
    /// 将主题应用到当前 ImGui 上下文（须在 CreateContext 之后、首帧之前调用）
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

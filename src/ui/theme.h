// ============================================================
// theme.h — ImGui 深霓虹主题
//
// 定义全局 UI 配色（深海军蓝背景 + 青/紫/粉强调色）与字体路径。
// 通过 Theme::apply() 一次性应用到 ImGui 上下文。
// ============================================================
#pragma once  // 防止头文件重复包含

namespace melody_matrix::ui {  // UI 子命名空间

/// ImGui 深霓虹主题配置
/// 背景: #1a1a2e（深海军蓝）
/// 强调色: #00fff5（青色）, #b300ff（紫色）, #ff006e（热粉色）
class Theme {
public:
    /// 加载 UI 字体（须在 CreateContext 之后、首帧 Render 之前调用）
    /// displayScale：相对 1080p 的高度比，写入 atlas 像素尺寸
    static bool loadFonts(float displayScale = 1.0f);

    /// 将主题应用到当前 ImGui 上下文（须在 CreateContext 之后、首帧之前调用）
    static void apply();  // 设置 ImGuiStyle 与全部 ImGuiCol 配色

    /// 从 apply() 时保存的基准样式按 displayScale 重新 ScaleAllSizes
    static void applyScaledStyle(float displayScale);

    /// 当前显示高度相对 1080p 的缩放（DisplaySize.y / 1080）
    static float displayScale();

    // ── 主题颜色常量 ──
    static constexpr float BG_R = 0.102f, BG_G = 0.102f, BG_B = 0.180f;  // 背景色 #1a1a2e
    static constexpr float CYAN_R = 0.0f,  CYAN_G = 1.0f,  CYAN_B = 0.96f; // 强调色 #00fff5
    static constexpr float PURP_R = 0.702f,PURP_G = 0.0f,  PURP_B = 1.0f;  // 强调色 #b300ff
    static constexpr float PINK_R = 1.0f,  PINK_G = 0.0f,  PINK_B = 0.431f; // 强调色 #ff006e

    // ── 字体 ──
    static constexpr const char* FONT_INTER = "assets/fonts/Inter-Regular.ttf";
    static constexpr const char* FONT_CJK = "assets/fonts/NotoSansSC-Regular.ttf";
    static constexpr float FONT_SIZE = 18.0f;  // 1080p 基准字号（像素）
};

} // namespace melody_matrix::ui

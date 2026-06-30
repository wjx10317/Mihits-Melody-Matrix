// ============================================================
// theme.cpp — ImGui 深霓虹主题实现
//
// 职责：
//   - 设置圆角、间距等 ImGuiStyle 参数
//   - 将深海军蓝 + 青/紫/粉配色应用到全部 ImGuiCol 条目
// ============================================================
#include "ui/theme.h"     // Theme 类声明
#include "util/logger.h"  // MM_LOG_* 日志宏

#include "imgui.h"  // ImGuiStyle、ImGuiCol 枚举

namespace melody_matrix::ui {  // UI 子命名空间

/// 应用深霓虹主题：圆角/间距 + 完整配色表
void Theme::apply() {
    MM_LOG_INFO("Theme", "Applying deep neon theme");  // 记录开始应用主题

    ImGuiStyle& style = ImGui::GetStyle();  // 获取当前 ImGui 样式引用
    ImVec4* colors = style.Colors;         // 指向颜色数组的指针

    // ── 圆角 ──
    style.WindowRounding = 8.0f;      // 窗口圆角半径
    style.ChildRounding = 6.0f;       // 子窗口圆角
    style.FrameRounding = 4.0f;       // 控件框圆角
    style.PopupRounding = 6.0f;       // 弹出层圆角
    style.ScrollbarRounding = 6.0f;   // 滚动条圆角
    style.GrabRounding = 4.0f;        // 滑块抓取区圆角
    style.TabRounding = 4.0f;         // 标签页圆角

    // ── 间距 ──
    style.WindowPadding = ImVec2(12, 12);   // 窗口内边距
    style.FramePadding = ImVec2(8, 4);      // 控件框内边距
    style.ItemSpacing = ImVec2(8, 6);      // 控件间距
    style.ItemInnerSpacing = ImVec2(6, 4);  // 控件内部间距
    style.WindowBorderSize = 0.0f;          // 窗口边框宽度（无边框）
    style.FrameBorderSize = 0.0f;           // 控件框边框宽度（无边框）

    // ── 调色板 ──
    // 背景色阶（#1a1a2e 系列）
    ImVec4 bgMain   = ImVec4(BG_R, BG_G, BG_B, 1.0f);           // 主背景 #1a1a2e
    ImVec4 bgLight  = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);        // 浅背景 #242438
    ImVec4 bgLighter= ImVec4(0.18f, 0.18f, 0.28f, 1.0f);        // 更浅背景 #2e2e47
    ImVec4 bgPanel  = ImVec4(BG_R, BG_G, BG_B, 0.94f);          // 面板背景（略透明）

    // 强调色
    ImVec4 cyan     = ImVec4(CYAN_R, CYAN_G, CYAN_B, 1.0f);     // 青色 #00fff5
    ImVec4 purple   = ImVec4(PURP_R, PURP_G, PURP_B, 1.0f);     // 紫色 #b300ff
    ImVec4 pink     = ImVec4(PINK_R, PINK_G, PINK_B, 1.0f);     // 热粉色 #ff006e
    ImVec4 cyanDim  = ImVec4(CYAN_R, CYAN_G, CYAN_B, 0.6f);     // 半透明青色
    ImVec4 purpleDim= ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);     // 半透明紫色

    // 文字
    ImVec4 textMain = ImVec4(0.95f, 0.95f, 0.98f, 1.0f);        // 主文字（近白）
    ImVec4 textDim  = ImVec4(0.55f, 0.55f, 0.65f, 1.0f);        // 弱化文字色

    // ── 应用到 ImGui 颜色表 ──
    colors[ImGuiCol_Text]                  = textMain;   // 正文文字色
    colors[ImGuiCol_TextDisabled]          = textDim;    // 禁用文字色
    colors[ImGuiCol_WindowBg]              = bgPanel;    // 窗口背景
    colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);  // 子窗口透明背景
    colors[ImGuiCol_PopupBg]               = bgPanel;    // 弹出层背景
    colors[ImGuiCol_Border]                = ImVec4(0.2f, 0.2f, 0.35f, 0.5f);  // 边框色
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);  // 边框阴影（无）
    colors[ImGuiCol_FrameBg]               = bgLighter;  // 控件框背景
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.35f, 1.0f);  // 悬停背景
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.26f, 0.42f, 1.0f);  // 激活背景
    colors[ImGuiCol_TitleBg]               = bgMain;   // 标题栏背景
    colors[ImGuiCol_TitleBgActive]         = bgLight;  // 激活标题栏
    colors[ImGuiCol_TitleBgCollapsed]      = bgMain;   // 折叠标题栏
    colors[ImGuiCol_MenuBarBg]             = bgLight;  // 菜单栏背景
    colors[ImGuiCol_ScrollbarBg]           = bgMain;   // 滚动条轨道
    colors[ImGuiCol_ScrollbarGrab]         = purpleDim;  // 滚动条滑块
    colors[ImGuiCol_ScrollbarGrabHovered]  = purple;     // 悬停滑块
    colors[ImGuiCol_ScrollbarGrabActive]   = pink;       // 激活滑块
    colors[ImGuiCol_CheckMark]             = cyan;       // 勾选标记
    colors[ImGuiCol_SliderGrab]            = cyanDim;    // 滑块抓取区
    colors[ImGuiCol_SliderGrabActive]      = cyan;       // 激活滑块
    colors[ImGuiCol_Button]                = ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);   // 按钮背景
    colors[ImGuiCol_ButtonHovered]         = ImVec4(PURP_R, PURP_G, PURP_B, 0.65f); // 悬停按钮
    colors[ImGuiCol_ButtonActive]          = ImVec4(PINK_R, PINK_G, PINK_B, 0.8f);  // 按下按钮
    colors[ImGuiCol_Header]                = ImVec4(PURP_R, PURP_G, PURP_B, 0.3f);  // 列表头
    colors[ImGuiCol_HeaderHovered]         = ImVec4(PURP_R, PURP_G, PURP_B, 0.5f);  // 悬停列表头
    colors[ImGuiCol_HeaderActive]          = ImVec4(PURP_R, PURP_G, PURP_B, 0.7f);  // 激活列表头
    colors[ImGuiCol_Separator]             = ImVec4(0.2f, 0.2f, 0.35f, 0.5f);       // 分隔线
    colors[ImGuiCol_SeparatorHovered]      = cyanDim;   // 悬停分隔线
    colors[ImGuiCol_SeparatorActive]       = cyan;      // 激活分隔线
    colors[ImGuiCol_ResizeGrip]            = purpleDim; // 缩放抓手
    colors[ImGuiCol_ResizeGripHovered]     = purple;    // 悬停缩放抓手
    colors[ImGuiCol_ResizeGripActive]      = pink;      // 激活缩放抓手
    colors[ImGuiCol_Tab]                   = bgLighter; // 标签页
    colors[ImGuiCol_TabHovered]            = ImVec4(PURP_R, PURP_G, PURP_B, 0.5f);  // 悬停标签
    colors[ImGuiCol_TabActive]             = ImVec4(PURP_R, PURP_G, PURP_B, 0.4f);    // 激活标签
    colors[ImGuiCol_TabUnfocused]          = bgLight;   // 未聚焦标签
    colors[ImGuiCol_TabUnfocusedActive]    = bgLighter; // 未聚焦激活标签
    colors[ImGuiCol_PlotLines]             = cyanDim;   // 折线图线条
    colors[ImGuiCol_PlotLinesHovered]      = cyan;      // 悬停折线
    colors[ImGuiCol_PlotHistogram]         = purple;    // 直方图
    colors[ImGuiCol_PlotHistogramHovered]  = pink;      // 悬停直方图
    colors[ImGuiCol_TableHeaderBg]         = bgLighter; // 表格头背景
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.2f, 0.2f, 0.35f, 0.6f);  // 表格强边框
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.2f, 0.2f, 0.35f, 0.3f);  // 表格弱边框
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);       // 表格行背景（透明）
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.02f);    // 表格交替行
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(CYAN_R, CYAN_G, CYAN_B, 0.2f);  // 选中文字背景
    colors[ImGuiCol_DragDropTarget]        = pink;      // 拖放目标高亮
    colors[ImGuiCol_NavHighlight]          = cyan;      // 键盘导航高亮
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);     // 窗口切换高亮
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);  // 窗口切换暗背景
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.6f);     // 模态窗口遮罩

    MM_LOG_INFO("Theme", "Deep neon theme applied");  // 记录主题应用完成
}

} // namespace melody_matrix::ui

// ============================================================
// ui_manager.cpp — ImGui 生命周期管理实现
//
// 职责：
//   - 创建 ImGui 上下文并绑定 SDL2 + OpenGL3 后端
//   - 每帧驱动 NewFrame → Render 流程
//   - 应用深霓虹主题
//   - 分辨率变化时重载字体并缩放 ImGuiStyle
// ============================================================
#include "ui/ui_manager.h"  // UIManager 类声明
#include "ui/theme.h"         // 深霓虹主题
#include "util/logger.h"      // MM_LOG_* 日志宏

#include <SDL.h>   // SDL 窗口与事件
#include <glad.h>  // OpenGL 函数加载

#include "imgui.h"                  // ImGui 核心 API
#include "imgui_impl_sdl2.h"        // ImGui SDL2 后端
#include "imgui_impl_opengl3.h"     // ImGui OpenGL3 后端

#include <cmath>

namespace melody_matrix::ui {  // UI 子命名空间

namespace {

float scaleFromWindowHeight(SDL_Window* window) {
    int winH = 0;
    if (window) {
        int winW = 0;
        SDL_GetWindowSize(window, &winW, &winH);
    }
    if (winH <= 0) {
        const float displayH = ImGui::GetIO().DisplaySize.y;
        winH = displayH > 0.0f ? static_cast<int>(displayH) : 1080;
    }
    return static_cast<float>(winH) / 1080.0f;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  初始化 / 关闭
// ══════════════════════════════════════════════════════════════════════════════

/// 初始化 ImGui：创建上下文、绑定后端、应用主题
bool UIManager::init(SDL_Window* window, SDL_GLContext glContext) {
    if (m_initialized) {  // 已初始化则跳过
        MM_LOG_WARN("UIManager", "Already initialized");  // 记录警告
        return true;  // 视为成功
    }

    MM_LOG_INFO("UIManager", "Initializing ImGui...");  // 记录开始初始化

    m_window = window;

    // ── ImGui 上下文 ──
    IMGUI_CHECKVERSION();           // 校验 ImGui 头文件与库版本一致
    ImGui::CreateContext();         // 创建 ImGui 上下文

    ImGuiIO& io = ImGui::GetIO();   // 获取 IO 配置引用
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 启用手柄导航
    io.IniFilename = nullptr; // 禁用 .ini 状态文件，避免窗口尺寸/位置被缓存
    io.FontGlobalScale = 1.0f;

    const float fontScale = scaleFromWindowHeight(window);
    Theme::loadFonts(fontScale);

    // ── SDL2 + OpenGL3 后端 ──
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {  // 初始化 SDL2 后端
        MM_LOG_ERROR("UIManager", "ImGui_ImplSDL2_InitForOpenGL failed");  // 记录错误
        return false;  // 初始化失败
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {  // 初始化 OpenGL3 后端（GLSL 330）
        MM_LOG_ERROR("UIManager", "ImGui_ImplOpenGL3_Init failed");  // 记录错误
        return false;  // 初始化失败
    }

    // ── 应用主题 ──
    Theme::apply();  // 设置深霓虹配色与圆角间距
    Theme::applyScaledStyle(fontScale);
    m_lastUiScale = fontScale;

    m_frameStatsOverlay.init(window);

    m_initialized = true;  // 标记初始化完成

    // ── 诊断信息 ──
    int w = 0, h = 0;  // 窗口宽高
    SDL_GetWindowSize(window, &w, &h);  // 查询 SDL 窗口尺寸
    MM_LOG_INFO("UIManager", "ImGui initialized successfully");  // 记录成功
    MM_LOG_INFO("UIManager", "  Window size: " + std::to_string(w) + "x" + std::to_string(h));  // 窗口尺寸
    MM_LOG_INFO("UIManager", "  DisplaySize: " + std::to_string((int)io.DisplaySize.x) + "x" + std::to_string((int)io.DisplaySize.y));  // ImGui 显示尺寸
    MM_LOG_INFO("UIManager", "  Fonts loaded: " + std::to_string(io.Fonts->Fonts.size()));  // 已加载字体数量
    MM_LOG_INFO("UIManager", "  UI scale: " + std::to_string(fontScale));
    return true;  // 初始化成功
}

/// 关闭 ImGui 并销毁上下文
void UIManager::shutdown() {
    if (!m_initialized) return;  // 未初始化则直接返回
    ImGui_ImplOpenGL3_Shutdown();  // 关闭 OpenGL3 后端
    ImGui_ImplSDL2_Shutdown();     // 关闭 SDL2 后端
    ImGui::DestroyContext();       // 销毁 ImGui 上下文
    m_initialized = false;           // 清除初始化标志
    m_window = nullptr;
    m_lastUiScale = -1.0f;
    MM_LOG_INFO("UIManager", "ImGui shut down");  // 记录关闭完成
}

void UIManager::syncUiScale(bool force) {
    if (!m_initialized) {
        return;
    }

    const float scale = scaleFromWindowHeight(m_window);
    if (!force && m_lastUiScale > 0.0f && std::abs(scale - m_lastUiScale) < 0.01f) {
        return;
    }

    MM_LOG_INFO("UIManager", "UI scale " + std::to_string(m_lastUiScale) + " -> " + std::to_string(scale));

    ImGui::GetIO().FontGlobalScale = 1.0f;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    Theme::loadFonts(scale);
    Theme::applyScaledStyle(scale);
    ImGui_ImplOpenGL3_CreateFontsTexture();
    m_lastUiScale = scale;
}

// ══════════════════════════════════════════════════════════════════════════════
//  每帧渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 开始新的 ImGui 帧
void UIManager::newFrame() {
    if (!m_initialized) return;           // 未初始化则跳过
    // DisplaySize 先更新，再按需重载字体；OpenGL NewFrame 会补建字体纹理
    ImGui_ImplSDL2_NewFrame();
    syncUiScale(false);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

/// 结束 ImGui 帧并提交 OpenGL 绘制
void UIManager::renderFrame() {
    if (!m_initialized) return;                              // 未初始化则跳过
    ImGui::Render();                                        // 生成绘制数据
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // 提交到 OpenGL
}

// ══════════════════════════════════════════════════════════════════════════════
//  输入
// ══════════════════════════════════════════════════════════════════════════════

/// 将 SDL 事件转发给 ImGui 处理
void UIManager::processEvent(void* sdlEvent) {
    if (!m_initialized) return;  // 未初始化则跳过
    ImGui_ImplSDL2_ProcessEvent(static_cast<SDL_Event*>(sdlEvent));  // 转发 SDL 事件
}

/// ImGui 是否正在捕获鼠标
bool UIManager::wantsMouseCapture() const {
    if (!m_initialized) return false;           // 未初始化时不捕获
    return ImGui::GetIO().WantCaptureMouse;     // 查询 ImGui IO 标志
}

/// ImGui 是否正在捕获键盘
bool UIManager::wantsKeyboardCapture() const {
    if (!m_initialized) return false;              // 未初始化时不捕获
    return ImGui::GetIO().WantCaptureKeyboard;     // 查询 ImGui IO 标志
}

void UIManager::setRenderFrameTimeMs(float frameTimeMs) {
    m_frameStatsOverlay.setFrameTimeMs(frameTimeMs);
}

void UIManager::renderFrameStatsOverlay() const {
    if (!m_initialized) return;
    m_frameStatsOverlay.render();
}

} // namespace melody_matrix::ui

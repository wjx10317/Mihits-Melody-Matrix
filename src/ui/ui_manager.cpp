// ============================================================
// ui_manager.cpp — ImGui 生命周期管理实现
//
// 职责：
//   - 创建 ImGui 上下文并绑定 SDL2 + OpenGL3 后端
//   - 每帧驱动 NewFrame → Render 流程
//   - 应用深霓虹主题
// ============================================================
#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "util/logger.h"

#include <SDL.h>
#include <glad.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

namespace melody_matrix::ui {

// ══════════════════════════════════════════════════════════════════════════════
//  初始化 / 关闭
// ══════════════════════════════════════════════════════════════════════════════

/// 初始化 ImGui：创建上下文、绑定后端、应用主题
bool UIManager::init(SDL_Window* window, SDL_GLContext glContext) {
    if (m_initialized) {
        MM_LOG_WARN("UIManager", "Already initialized");
        return true;
    }

    MM_LOG_INFO("UIManager", "Initializing ImGui...");

    // ── ImGui 上下文 ──
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // 禁用 .ini 状态文件，避免窗口尺寸/位置被缓存

    // ── SDL2 + OpenGL3 后端 ──
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
        MM_LOG_ERROR("UIManager", "ImGui_ImplSDL2_InitForOpenGL failed");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        MM_LOG_ERROR("UIManager", "ImGui_ImplOpenGL3_Init failed");
        return false;
    }

    // ── 应用主题 ──
    Theme::apply();

    m_initialized = true;

    // ── 诊断信息 ──
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    MM_LOG_INFO("UIManager", "ImGui initialized successfully");
    MM_LOG_INFO("UIManager", "  Window size: " + std::to_string(w) + "x" + std::to_string(h));
    MM_LOG_INFO("UIManager", "  DisplaySize: " + std::to_string((int)io.DisplaySize.x) + "x" + std::to_string((int)io.DisplaySize.y));
    MM_LOG_INFO("UIManager", "  Fonts loaded: " + std::to_string(io.Fonts->Fonts.size()));
    return true;
}

/// 关闭 ImGui 并销毁上下文
void UIManager::shutdown() {
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    MM_LOG_INFO("UIManager", "ImGui shut down");
}

// ══════════════════════════════════════════════════════════════════════════════
//  每帧渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 开始新的 ImGui 帧
void UIManager::newFrame() {
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

/// 结束 ImGui 帧并提交 OpenGL 绘制
void UIManager::renderFrame() {
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ══════════════════════════════════════════════════════════════════════════════
//  输入
// ══════════════════════════════════════════════════════════════════════════════

/// 将 SDL 事件转发给 ImGui 处理
void UIManager::processEvent(void* sdlEvent) {
    if (!m_initialized) return;
    ImGui_ImplSDL2_ProcessEvent(static_cast<SDL_Event*>(sdlEvent));
}

/// ImGui 是否正在捕获鼠标
bool UIManager::wantsMouseCapture() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

/// ImGui 是否正在捕获键盘
bool UIManager::wantsKeyboardCapture() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace melody_matrix::ui

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "util/logger.h"

#include <SDL.h>
#include <glad.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

namespace melody_matrix::ui {

bool UIManager::init(SDL_Window* window, SDL_GLContext glContext) {
    if (m_initialized) {
        MM_LOG_WARN("UIManager", "Already initialized");
        return true;
    }

    MM_LOG_INFO("UIManager", "Initializing ImGui...");

    // ── ImGui context ──
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // Disable .ini state file to prevent window size/pos caching

    // ── SDL2 + OpenGL3 backend ──
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
        MM_LOG_ERROR("UIManager", "ImGui_ImplSDL2_InitForOpenGL failed");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        MM_LOG_ERROR("UIManager", "ImGui_ImplOpenGL3_Init failed");
        return false;
    }

    // ── Apply theme ──
    Theme::apply();

    m_initialized = true;

    // ── Diagnostic info ──
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    MM_LOG_INFO("UIManager", "ImGui initialized successfully");
    MM_LOG_INFO("UIManager", "  Window size: " + std::to_string(w) + "x" + std::to_string(h));
    MM_LOG_INFO("UIManager", "  DisplaySize: " + std::to_string((int)io.DisplaySize.x) + "x" + std::to_string((int)io.DisplaySize.y));
    MM_LOG_INFO("UIManager", "  Fonts loaded: " + std::to_string(io.Fonts->Fonts.size()));
    return true;
}

void UIManager::newFrame() {
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void UIManager::renderFrame() {
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::processEvent(void* sdlEvent) {
    if (!m_initialized) return;
    ImGui_ImplSDL2_ProcessEvent(static_cast<SDL_Event*>(sdlEvent));
}

void UIManager::shutdown() {
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    MM_LOG_INFO("UIManager", "ImGui shut down");
}

bool UIManager::wantsMouseCapture() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool UIManager::wantsKeyboardCapture() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace melody_matrix::ui

#pragma once

#include "core/clock.h"
#include "core/state_manager.h"
#include "renderer/renderer.h"
#include "ui/ui_manager.h"
#include "util/event_manager.h"

#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
typedef struct SDL_KeyboardEvent SDL_KeyboardEvent;
typedef void* SDL_GLContext;

namespace melody_matrix::core {

/// 支持的分辨率条目
struct Resolution {
    int width;
    int height;
    const char* label; // 例如 "1920×1080"
};

/// 应用内核 — 单例，拥有所有子系统并驱动主循环。
/// 固定逻辑步长（240 Hz）+ 可变渲染步长架构。
class Kernel {
public:
    /// 获取单例实例
    static Kernel& instance();

    /// 初始化所有子系统，创建窗口和 GL 上下文
    bool init(const std::string& title = "Melody Matrix", int width = 1920, int height = 1080);

    /// 运行主循环（阻塞直到退出）
    void run();

    /// 关闭所有子系统并清理
    void shutdown();

    /// 请求退出（可从任何状态调用）
    void requestExit() { m_running = false; }

    // ── 分辨率管理 ──
    /// 获取支持的分辨率列表
    static const std::vector<Resolution>& supportedResolutions();

    /// 更改窗口分辨率（并保存到配置）
    void setResolution(int width, int height);

    /// 获取当前分辨率（物理像素，与分辨率列表匹配）
    void getCurrentResolution(int& width, int& height) const;

    /// 切换全屏模式
    void setFullscreen(bool fullscreen);

    /// 当前是否全屏？
    bool isFullscreen() const { return m_fullscreen; }

    /// 获取显示器原生分辨率
    void getNativeResolution(int& width, int& height) const { width = m_displayWidth; height = m_displayHeight; }

    // ── 子系统访问器 ──
    Clock& clock() { return m_clock; }
    StateManager& stateManager() { return m_stateManager; }
    util::EventManager& eventManager() { return m_eventManager; }
    renderer::Renderer& renderer() { return m_renderer; }
    ui::UIManager& uiManager() { return m_uiManager; }
    SDL_Window* window() const { return m_window; }

private:
    Kernel() = default;
    ~Kernel() = default;
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    /// 将 SDL 事件泵入输入系统和 ImGui
    void pumpInputEvents();
    void syncPlayingClock();
    void dispatchGameplayKeyEvent(const SDL_KeyboardEvent& keyEvent, bool pressed);

    /// 根据当前分辨率应用窗口模式（无边框/有边框+clamp）
    void applyWindowMode();

    // ── 子系统 ──
    Clock m_clock;
    StateManager m_stateManager;
    util::EventManager m_eventManager;
    renderer::Renderer m_renderer;
    ui::UIManager m_uiManager;

    // ── Window ──
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;

    // ── 分辨率状态 ──
    int m_displayWidth = 1920;   // 原生显示器物理像素
    int m_displayHeight = 1080;
    int m_windowWidth = 1920;    // 用户选择的分辨率（物理像素）
    int m_windowHeight = 1080;
    bool m_fullscreen = false;

    bool m_running = false;
    bool m_initialized = false;

};

} // namespace melody_matrix::core

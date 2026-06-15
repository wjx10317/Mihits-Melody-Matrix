#pragma once

#include <string>

struct SDL_Window;
typedef void* SDL_GLContext;

namespace melody_matrix::ui {

/// 管理 ImGui 初始化、每帧渲染和主题应用。
/// 渲染管线的第 4 层 — 在所有 OpenGL 层之后绘制。
class UIManager {
public:
    UIManager() = default;
    ~UIManager() = default;

    /// 使用 SDL2 + OpenGL3 后端初始化 ImGui
    bool init(SDL_Window* window, SDL_GLContext glContext);

    /// 开始新的 ImGui 帧（在任何 ImGui 调用之前调用）
    void newFrame();

    /// 完成 ImGui 帧并渲染（在所有 ImGui 调用之后调用）
    void renderFrame();

    /// 处理 ImGui 的 SDL 事件（从事件泵调用）
    void processEvent(void* sdlEvent);

    /// 关闭 ImGui 并清理
    void shutdown();

    /// 检查 ImGui 是否想要捕获鼠标/键盘输入
    bool wantsMouseCapture() const;
    bool wantsKeyboardCapture() const;

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace melody_matrix::ui

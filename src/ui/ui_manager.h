// ============================================================
// ui_manager.h — ImGui 生命周期管理
//
// 封装 ImGui 的初始化、每帧 NewFrame/Render、SDL 事件转发与
// 输入捕获查询。位于渲染管线第 4 层（OpenGL 层之后绘制）。
// ============================================================
#pragma once

#include <string>

struct SDL_Window;
typedef void* SDL_GLContext;

namespace melody_matrix::ui {

/// ImGui 初始化与每帧渲染管理器
class UIManager {
public:
    UIManager() = default;
    ~UIManager() = default;

    /// 使用 SDL2 + OpenGL3 后端初始化 ImGui 上下文
    bool init(SDL_Window* window, SDL_GLContext glContext);

    /// 开始新的 ImGui 帧（须在任意 ImGui 调用之前）
    void newFrame();

    /// 结束 ImGui 帧并提交 OpenGL 绘制
    void renderFrame();

    /// 将 SDL 事件转发给 ImGui（从主循环事件泵调用）
    void processEvent(void* sdlEvent);

    /// 关闭 ImGui 并释放上下文
    void shutdown();

    /// ImGui 是否正在捕获鼠标输入
    bool wantsMouseCapture() const;
    /// ImGui 是否正在捕获键盘输入
    bool wantsKeyboardCapture() const;

    /// 是否已完成初始化
    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace melody_matrix::ui

// ============================================================
// ui_manager.h — ImGui 生命周期管理
//
// 封装 ImGui 的初始化、每帧 NewFrame/Render、SDL 事件转发与
// 输入捕获查询。位于渲染管线第 4 层（OpenGL 层之后绘制）。
// ============================================================
#pragma once  // 防止头文件重复包含

#include <string>  // std::string（日志诊断信息）

struct SDL_Window;           // SDL 窗口前向声明
typedef void* SDL_GLContext; // OpenGL 上下文句柄类型别名

namespace melody_matrix::ui {  // UI 子命名空间

/// ImGui 初始化与每帧渲染管理器
class UIManager {
public:
    UIManager() = default;   // 默认构造
    ~UIManager() = default;  // 默认析构

    /// 使用 SDL2 + OpenGL3 后端初始化 ImGui 上下文
    bool init(SDL_Window* window, SDL_GLContext glContext);  // 初始化 ImGui 及后端

    /// 开始新的 ImGui 帧（须在任意 ImGui 调用之前）
    void newFrame();  // 驱动 NewFrame 流程

    /// 结束 ImGui 帧并提交 OpenGL 绘制
    void renderFrame();  // 渲染 ImGui 绘制数据

    /// 将 SDL 事件转发给 ImGui（从主循环事件泵调用）
    void processEvent(void* sdlEvent);  // 转发 SDL_Event 给 ImGui

    /// 关闭 ImGui 并释放上下文
    void shutdown();  // 销毁 ImGui 上下文与后端

    /// ImGui 是否正在捕获鼠标输入
    bool wantsMouseCapture() const;  // 查询 WantCaptureMouse
    /// ImGui 是否正在捕获键盘输入
    bool wantsKeyboardCapture() const;  // 查询 WantCaptureKeyboard

    /// 是否已完成初始化
    bool isInitialized() const { return m_initialized; }  // 返回初始化标志

private:
    bool m_initialized = false;  // 是否已完成 init()
};

} // namespace melody_matrix::ui

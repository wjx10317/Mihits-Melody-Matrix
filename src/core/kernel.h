/**
 * @file kernel.h
 * @brief 应用内核单例
 *
 * 文件职责：
 *   声明 Melody Matrix 核心子系统（Clock、StateManager、Renderer 等）及主循环接口。
 *
 * 主要依赖：
 *   clock.h、state_manager.h、renderer/renderer.h、ui/ui_manager.h、util/event_manager.h、SDL。
 *
 * 在项目中的用法：
 *   main 中 Kernel::instance().init() → 注册状态 → run() → shutdown()；
 *   各模块通过 instance() 访问 clock、stateManager、renderer 等。
 */
#pragma once

#include "core/clock.h"              // 音频驱动歌曲时钟
#include "core/state_manager.h"      // 游戏状态机管理
#include "renderer/renderer.h"       // OpenGL 谱面/背景渲染
#include "ui/ui_manager.h"           // ImGui UI 层
#include "util/event_manager.h"      // 应用内事件总线

#include <memory>   // 智能指针等（头文件预留）
#include <string>   // init() 窗口标题参数
#include <vector>   // supportedResolutions() 返回列表

struct SDL_Window;                              // SDL 窗口前向声明，避免头文件拉入 SDL.h
typedef struct SDL_KeyboardEvent SDL_KeyboardEvent;  // 键盘事件前向声明
typedef void* SDL_GLContext;                   // OpenGL 上下文句柄类型

namespace melody_matrix::core {

/**
 * @brief 支持的分辨率条目
 *
 * 用于设置菜单展示；label 为显示字符串（如 "1920×1080" 或 "NATIVE"）。
 */
struct Resolution {
    int width;          ///< 物理像素宽度
    int height;         ///< 物理像素高度
    const char* label;  ///< UI 显示标签
};

/**
 * @brief 应用内核单例
 *
 * 固定逻辑步长 240 Hz + 可变渲染步长；拥有窗口、GL 上下文及全部子系统。
 */
class Kernel {
public:
    /**
     * @brief 获取全局单例
     * @return Kernel 唯一实例引用
     */
    static Kernel& instance();

    /**
     * @brief 初始化 SDL、窗口、OpenGL、Renderer、UIManager
     * @param title 窗口标题
     * @param width 默认宽度（实际从 config 读取，参数保留兼容）
     * @param height 默认高度
     * @return 成功 true
     */
    bool init(const std::string& title = "Melody Matrix", int width = 1920, int height = 1080);

    /**
     * @brief 运行主循环（阻塞直到 requestExit）
     */
    void run();

    /**
     * @brief 关闭子系统、保存配置、销毁窗口
     */
    void shutdown();

    /**
     * @brief 请求退出主循环
     *
     * 可从任意状态或输入处理中调用。
     */
    void requestExit() { m_running = false; }  // 置 false 使 run() 中 while 退出

    // ── 分辨率管理 ──

    /**
     * @brief 获取支持的分辨率列表（含 NATIVE + 常见分辨率）
     * @return 分辨率向量引用
     */
    static const std::vector<Resolution>& supportedResolutions();

    /**
     * @brief 更改窗口分辨率并写入配置
     * @param width 客户端区域宽度（物理像素）
     * @param height 客户端区域高度
     */
    void setResolution(int width, int height);

    /**
     * @brief 获取当前配置的分辨率
     * @param width 输出宽度
     * @param height 输出高度
     */
    void getCurrentResolution(int& width, int& height) const;

    /**
     * @brief 切换全屏/窗口模式
     * @param fullscreen true 进入全屏桌面模式
     */
    void setFullscreen(bool fullscreen);

    /**
     * @brief 当前是否全屏
     * @return 全屏 true
     */
    bool isFullscreen() const { return m_fullscreen; }

    /**
     * @brief 获取显示器原生分辨率
     * @param width 输出物理宽度
     * @param height 输出物理高度
     */
    void getNativeResolution(int& width, int& height) const { width = m_displayWidth; height = m_displayHeight; }

    // ── 子系统访问器 ──

    /** @return 游戏时钟引用 */
    Clock& clock() { return m_clock; }
    /** @return 状态管理器引用 */
    StateManager& stateManager() { return m_stateManager; }
    /** @return 事件总线引用 */
    util::EventManager& eventManager() { return m_eventManager; }
    /** @return OpenGL 渲染器引用 */
    renderer::Renderer& renderer() { return m_renderer; }
    /** @return ImGui UI 管理器引用 */
    ui::UIManager& uiManager() { return m_uiManager; }
    /** @return SDL 窗口指针 */
    SDL_Window* window() const { return m_window; }

private:
    Kernel() = default;
    ~Kernel() = default;
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    /**
     * @brief 处理 SDL 事件队列（输入、退出、窗口 resize）
     */
    void pumpInputEvents();

    /**
     * @brief 在 Playing 状态下从音频同步 Clock
     */
    void syncPlayingClock();

    /**
     * @brief 将按键事件分派给 PlayingState（带事件 timestamp 歌曲时间）
     * @param keyEvent SDL 键盘事件
     * @param pressed true 按下，false 释放
     */
    void dispatchGameplayKeyEvent(const SDL_KeyboardEvent& keyEvent, bool pressed);

    /**
     * @brief 根据分辨率应用无边框/有边框窗口模式
     */
    void applyWindowMode();

    // ── 子系统 ──
    Clock m_clock;                      ///< 权威歌曲时钟
    StateManager m_stateManager;        ///< 游戏状态机
    util::EventManager m_eventManager;  ///< 应用内事件分发
    renderer::Renderer m_renderer;      ///< OpenGL 渲染器
    ui::UIManager m_uiManager;          ///< ImGui 管理器

    // ── 窗口 ──
    SDL_Window* m_window = nullptr;       ///< SDL 主窗口
    SDL_GLContext m_glContext = nullptr;  ///< OpenGL 3.3 Core 上下文

    // ── 分辨率状态 ──
    int m_displayWidth = 1920;   ///< 显示器原生物理像素宽
    int m_displayHeight = 1080;  ///< 显示器原生物理像素高
    int m_windowWidth = 1920;    ///< 用户选择的分辨率宽
    int m_windowHeight = 1080;   ///< 用户选择的分辨率高
    bool m_fullscreen = false;   ///< 是否全屏桌面模式

    bool m_running = false;      ///< 主循环是否继续运行
    bool m_initialized = false;  ///< init() 是否已成功完成
};

} // namespace melody_matrix::core

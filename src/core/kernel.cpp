/**
 * @file kernel.cpp
 * @brief Kernel 单例实现：初始化、主循环、输入与分辨率管理
 *
 * 文件职责：
 *   实现 SDL/OpenGL 窗口创建、240Hz 固定步长 + VSync 渲染循环、
 *   输入分派、音频时钟同步及分辨率/全屏切换。
 *
 * 主要依赖：
 *   kernel.h、platform/config、PlayingState/SongSelectState、SDL、GLAD、ImGui。
 *
 * 在项目中的用法：
 *   由 main.cpp 通过 Kernel::instance() 驱动；子系统通过 kernel 访问器获取依赖。
 */
#include "kernel.h"                         // Kernel 类及子系统成员
#include "game_state.h"                     // GameState 枚举，用于状态判断
#include "core/states/playing_state.h"      // PlayingState：游玩按键与音频时钟同步
#include "platform/config.h"                // config.ini 读写：分辨率、全屏、visual lead
#include "core/states/song_select_state.h"  // SongSelectState：ESC 是否被弹窗消费
#include "time/host_clock.h"                // HostClock：与 Clock 同域的按键时刻
#include "util/logger.h"                    // MM_LOG_INFO / WARN / FATAL 日志宏
#include "util/exceptions.h"                // 异常类型（日志/错误处理预留）
#include "renderer/texture_cache.h"         // 主线程 GL 纹理上传队列

#include <SDL.h>        // SDL 窗口、事件、OpenGL 上下文
#include <glad.h>       // OpenGL 3.3 函数加载器
#include <chrono>       // steady_clock：主循环帧时间测量
#include <iostream>     // 标准输出（调试预留）
#include <algorithm>    // std::min：clamp 帧时间与窗口尺寸

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════
//  分辨率列表（设置菜单可选）
// ══════════════════════════════════════════════════════

static std::vector<Resolution> s_resolutions;  // 懒构建的分辨率列表缓存

/**
 * @brief 构建支持的分辨率列表
 *
 * 首项为显示器 NATIVE 分辨率，其后追加常见分辨率并去重。
 */
static void buildResolutionList() {
    s_resolutions.clear();  // 重建前先清空
    SDL_DisplayMode desktop;  // 桌面/原生显示模式
    // 成功获取主显示器桌面模式则加入 NATIVE 项
    if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
        s_resolutions.push_back({ desktop.w, desktop.h, "NATIVE" });
    }
    static const Resolution common[] = {  // 预设常见分辨率表
        { 2560, 1600, "2560x1600" },
        { 2560, 1440, "2560x1440" },
        { 1920, 1200, "1920x1200" },
        { 1920, 1080, "1920x1080" },
        { 1680, 1050, "1680x1050" },
        { 1440,  900, "1440x900"  },
        { 1280,  720, "1280x720"  },
    };
    for (const auto& r : common) {
        bool dup = false;  // 是否与已有项（含 NATIVE）重复
        for (const auto& existing : s_resolutions) {
            // 宽高相同视为重复，跳过
            if (existing.width == r.width && existing.height == r.height) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            s_resolutions.push_back(r);  // 追加非重复分辨率
        }
    }
}

// ══════════════════════════════════════════════════════
//  游玩输入与时钟同步
// ══════════════════════════════════════════════════════

/**
 * @brief 将键盘事件分派给 PlayingState（HostClock 换算歌曲时间）
 * @param keyEvent SDL 键盘事件（仅用 keysym；不用 event.timestamp）
 * @param pressed 按下或释放
 */
void Kernel::dispatchGameplayKeyEvent(const SDL_KeyboardEvent& keyEvent, bool pressed) {
    // 非 Playing 状态不处理游玩按键
    if (m_stateManager.currentState() != GameState::Playing) return;
    auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
    if (!playing) return;

    // 在 poll 阶段立即判定。歌曲时间只用 HostClock（与 Clock 同域）。
    // 已删除 SDL timestamp / expandSdlEventTimestamp 路径。
    // 后续：改为按键瞬间 Host 戳（Raw Input）。
    const int64_t hostMs = time::HostClock::nowMs();
    const int64_t eventSongTimeMs = m_clock.songTimeAtHostMs(hostMs);
    playing->handleKeyEvent(static_cast<int32_t>(keyEvent.keysym.sym), pressed, eventSongTimeMs);
}

/**
 * @brief Playing 状态下从音频引擎拉取 cursor 并同步 Clock
 */
void Kernel::syncPlayingClock() {
    // 仅 Playing 状态需要从音频同步时钟
    if (m_stateManager.currentState() != GameState::Playing) return;
    auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
    if (playing) {
        playing->syncClockFromAudio();  // 从 AudioEngine 拉 cursor 写入 Clock
    }
}

const std::vector<Resolution>& Kernel::supportedResolutions() {
    // 懒构建：首次访问时生成列表
    if (s_resolutions.empty()) {
        buildResolutionList();
    }
    return s_resolutions;
}

Kernel& Kernel::instance() {
    static Kernel s_instance;  // Meyers 单例，程序生命周期内唯一
    return s_instance;
}

// ══════════════════════════════════════════════════════
//  窗口尺寸辅助
// ══════════════════════════════════════════════════════

/**
 * @brief 将有边框窗口的客户端尺寸限制在显示器可用区域内
 * @param window SDL 窗口（可为 null，使用估算边框）
 * @param w 输入/输出宽度
 * @param h 输入/输出高度
 */
static void clampToUsable(SDL_Window* window, int& w, int& h) {
    int displayIdx = window ? SDL_GetWindowDisplayIndex(window) : 0;  // 窗口所在显示器索引
    SDL_Rect usable;  // 扣除任务栏后的可用区域
    if (SDL_GetDisplayUsableBounds(displayIdx, &usable) != 0) return;  // 获取失败则保持原尺寸

    int top = 0, left = 0, bottom = 0, right = 0;  // 窗口边框占用像素
    if (window) {
        SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);  // 实测边框
    } else {
        top = 31; left = 8; bottom = 8; right = 8;  // 创建窗口前用 Windows 典型值估算
    }

    int maxW = usable.w - left - right;   // 客户端最大可用宽
    int maxH = usable.h - top - bottom;     // 客户端最大可用高
    if (maxW < 640) maxW = 640;   // 下限保证最小可玩宽度
    if (maxH < 480) maxH = 480;   // 下限保证最小可玩高度

    w = std::min(w, maxW);  // 将请求宽度 clamp 到可用范围
    h = std::min(h, maxH);
}

/**
 * @brief 根据当前分辨率应用窗口模式
 *
 * 分辨率 ≥ 显示器原生 → 无边框全屏客户端；否则有边框并 clamp 到可用区域。
 * 与全屏模式独立；全屏时本函数 early return。
 */
void Kernel::applyWindowMode() {
    // 无窗口或已全屏时不调整窗口模式
    if (!m_window || m_fullscreen) return;

    // 用户分辨率不小于原生 → 无边框铺满显示器
    bool needsBorderless = (m_windowWidth >= m_displayWidth && m_windowHeight >= m_displayHeight);

    if (needsBorderless) {
        SDL_SetWindowBordered(m_window, SDL_FALSE);                    // 去掉标题栏边框
        SDL_SetWindowSize(m_window, m_displayWidth, m_displayHeight);    // 铺满原生分辨率
        SDL_SetWindowPosition(m_window, 0, 0);                           // 左上角对齐
    } else {
        SDL_SetWindowBordered(m_window, SDL_TRUE);                       // 恢复有边框窗口
        int setW = m_windowWidth, setH = m_windowHeight;                 // 用户选择的分辨率
        clampToUsable(m_window, setW, setH);                             // 限制在可用区域内
        SDL_SetWindowSize(m_window, setW, setH);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);  // 居中显示
    }

    MM_LOG_INFO("Kernel", "Window mode: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight)
        + (needsBorderless ? " (borderless)" : " (bordered)"));
}

// ══════════════════════════════════════════════════════
//  初始化
// ══════════════════════════════════════════════════════

/**
 * @brief 初始化 SDL、OpenGL 3.3 Core、Renderer 与 UIManager
 * @return 任一步骤失败返回 false
 */
bool Kernel::init(const std::string& title, int /*width*/, int /*height*/) {
    MM_LOG_INFO("Kernel", "Initializing Melody Matrix...");

    // Per-Monitor V2 DPI：SDL 坐标直接为物理像素，无需逻辑/物理换算
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    // 初始化视频、音频、定时器子系统
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        MM_LOG_FATAL("Kernel", std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    // ── 检测显示器原生分辨率 ──
    {
        SDL_DisplayMode desktop;
        if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
            m_displayWidth = desktop.w;   // 记录原生物理宽
            m_displayHeight = desktop.h;  // 记录原生物理高
            MM_LOG_INFO("Kernel", "Display native: " +
                std::to_string(desktop.w) + "x" + std::to_string(desktop.h));
        } else {
            MM_LOG_WARN("Kernel", "Cannot detect display resolution, using defaults");
        }
    }

    buildResolutionList();  // 构建设置菜单可选分辨率列表

    // ── 从 config.ini 加载分辨率与全屏设置 ──
    platform::Config::load();  // 读取配置文件到内存
    m_windowWidth = platform::Config::getInt(platform::Config::KEY_RESOLUTION_W, 0);
    m_windowHeight = platform::Config::getInt(platform::Config::KEY_RESOLUTION_H, 0);
    m_fullscreen = platform::Config::getInt(platform::Config::KEY_FULLSCREEN, 0) != 0;

    // 配置无效或未保存时使用默认 1920x1080
    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        m_windowWidth = 1920;
        m_windowHeight = 1080;
        MM_LOG_INFO("Kernel", "No saved resolution, using default: 1920x1080");
    }

    // ── OpenGL 3.3 Core 属性 ──
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);   // 双缓冲，配合 SwapWindow
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);    // 深度缓冲位数
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);     // 模板缓冲位数

    // ── 创建窗口 ──
    int createW = m_windowWidth;   // 实际创建时的客户端宽
    int createH = m_windowHeight;
    if (!m_fullscreen) {
        clampToUsable(nullptr, createW, createH);  // 窗口模式先 clamp，避免超出屏幕
    }

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
                       | SDL_WINDOW_ALLOW_HIGHDPI;  // OpenGL + 可调整大小 + HiDPI
    if (m_fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;  // 全屏桌面模式（非独占）
    }
    m_window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        createW, createH,
        windowFlags
    );
    if (!m_window) {
        MM_LOG_FATAL("Kernel", std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetWindowMinimumSize(m_window, 800, 600);  // 限制用户可缩到的最小尺寸

    // ── GL 上下文与 GLAD ──
    m_glContext = SDL_GL_CreateContext(m_window);  // 创建 OpenGL 上下文并设为当前
    if (!m_glContext) {
        MM_LOG_FATAL("Kernel", std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    // 通过 SDL 的 proc 地址加载 OpenGL 3.3 函数指针
    if (!gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        MM_LOG_FATAL("Kernel", "Failed to initialize GLAD (OpenGL 3.3 Core)");
        SDL_GL_DeleteContext(m_glContext);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    GLint major = 0, minor = 0;  // 实际 OpenGL 版本号
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    MM_LOG_INFO("Kernel", "OpenGL " + std::to_string(major) + "." + std::to_string(minor));

    // 要求至少 OpenGL 3.3 Core
    if (major < 3 || (major == 3 && minor < 3)) {
        MM_LOG_FATAL("Kernel", "OpenGL 3.3 Core not available.");
        return false;
    }

    SDL_GL_SetSwapInterval(1);  // 开启 VSync，SwapWindow 与显示器刷新同步

    glEnable(GL_BLEND);                              // 启用 alpha 混合（谱面/UI）
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.102f, 0.102f, 0.180f, 1.0f); // #1a1a2e 默认清屏色

    if (!m_renderer.init()) {
        MM_LOG_WARN("Kernel", "Renderer initialization failed");  // 非致命，继续运行
    }
    if (!m_uiManager.init(m_window, m_glContext)) {
        MM_LOG_WARN("Kernel", "UI manager initialization failed");
    }

    if (!m_fullscreen) {
        applyWindowMode();  // 窗口模式：应用无边框/有边框策略
    }

    m_initialized = true;
    return true;
}

// ══════════════════════════════════════════════════════
//  主循环（240 Hz 固定步长 + VSync 渲染）
// ══════════════════════════════════════════════════════

/**
 * @brief 固定步长更新 + 可变步长渲染主循环
 *
 * 每帧：同步时钟 → 累积 fixed update（每步先 poll 输入再 update）→ 清屏 → 带 visual lead 渲染谱面
 * → ImGui → SwapBuffers。
 */
void Kernel::run() {
    // 未 init 成功则拒绝运行
    if (!m_initialized) {
        MM_LOG_FATAL("Kernel", "Cannot run: Kernel not initialized");
        return;
    }

    m_running = true;
    constexpr double FIXED_DT = 1.0 / 240.0;  // 固定逻辑步长 1/240 秒
    constexpr double kFixedDtMs = FIXED_DT * 1000.0;
    double accumulator = 0.0;                   // 累积未消费的帧时间
    auto lastTime = std::chrono::steady_clock::now();  // 上一帧时刻
    auto lastFixedStepEnd = lastTime;           // 上一次 fixed update 结束时刻（跨帧）
    bool haveLastFixedStepEnd = false;

    MM_LOG_INFO("Kernel", "Main loop started");

    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        double frameTime = std::chrono::duration<double>(now - lastTime).count();  // 本帧真实耗时（秒）
        lastTime = now;
        frameTime = std::min(frameTime, 0.25); // 防 spiral of death：单帧最多计 250ms
        accumulator += frameTime;  // 累加到固定步长 accumulator

        // 先同步音频时钟；每步 fixed update 前 poll 输入，避免「本帧只 poll 一次、
        // 后续 240Hz tick 已 auto-miss」的竞态
        syncPlayingClock();

        int fixedSteps = 0;
        double maxStepWallMs = 0.0;    // 单步 poll+update 墙钟耗时上限
        double maxUpdateGapMs = 0.0;   // 相邻两次 update 开始间隔上限
        while (accumulator >= FIXED_DT) {
            const auto stepBegin = std::chrono::steady_clock::now();
            if (haveLastFixedStepEnd) {
                const double gapMs = std::chrono::duration<double, std::milli>(
                    stepBegin - lastFixedStepEnd).count();
                maxUpdateGapMs = std::max(maxUpdateGapMs, gapMs);
            }

            pumpInputEvents();
            m_stateManager.update(static_cast<float>(FIXED_DT));
            accumulator -= FIXED_DT;
            ++fixedSteps;

            const auto stepEnd = std::chrono::steady_clock::now();
            const double stepMs = std::chrono::duration<double, std::milli>(
                stepEnd - stepBegin).count();
            maxStepWallMs = std::max(maxStepWallMs, stepMs);
            lastFixedStepEnd = stepEnd;
            haveLastFixedStepEnd = true;
        }
        if (fixedSteps > 0) {
            const double frameMs = frameTime * 1000.0;
            MM_LOG_DEBUG("Kernel",
                "Update timing: frame=" + std::to_string(frameMs) + "ms"
                + " steps=" + std::to_string(fixedSteps)
                + " stepWallMax=" + std::to_string(maxStepWallMs) + "ms"
                + " updateGapMax=" + std::to_string(maxUpdateGapMs) + "ms"
                + " expectStep=" + std::to_string(kFixedDtMs) + "ms");
            if (maxStepWallMs > kFixedDtMs * 1.5) {
                MM_LOG_WARN("Kernel",
                    "Fixed update slower than realtime: stepWallMax="
                    + std::to_string(maxStepWallMs) + "ms expect<="
                    + std::to_string(kFixedDtMs) + "ms");
            }
        }
        if (fixedSteps == 0) {
            pumpInputEvents();  // 本帧不足一拍时仍处理退出/ImGui/菜单输入
        }

        // 视口使用 framebuffer 物理像素尺寸
        int drawW = 0, drawH = 0;
        SDL_GL_GetDrawableSize(m_window, &drawW, &drawH);  // HiDPI 下为物理像素
        glViewport(0, 0, drawW, drawH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // 清颜色与深度缓冲

        renderer::TextureCache::instance().processPendingUploads(8);

        // 视觉超前：补偿 VSync/扫描延迟，使 note 中心与判定时刻对齐
        const int64_t songNowMs = m_clock.interpolatedNowMs();  // 当前插值歌曲时间
        const int64_t configLeadMs = platform::Config::getInt(
            platform::Config::KEY_VISUAL_LEAD, 16);  // 用户可配置的 visual lead
        const int64_t frameLeadMs = static_cast<int64_t>(frameTime * 500.0 + 0.5);  // 半帧时间补偿
        const int64_t visualTimeMs = songNowMs + configLeadMs + frameLeadMs;  // 渲染用超前时间

        m_renderer.renderFrame(visualTimeMs);  // OpenGL 谱面/背景渲染

        m_uiManager.setRenderFrameTimeMs(static_cast<float>(frameTime * 1000.0));

        m_uiManager.newFrame();       // ImGui 新帧开始
        m_stateManager.render();      // 当前状态 ImGui UI
        m_uiManager.renderFrameStatsOverlay();  // 右下角帧统计（全状态）
        m_uiManager.renderFrame();    // 提交 ImGui 绘制

        SDL_GL_SwapWindow(m_window);  // 交换前后缓冲，VSync 等待
    }

    MM_LOG_INFO("Kernel", "Main loop ended");
}

// ══════════════════════════════════════════════════════
//  关闭与分辨率 API
// ══════════════════════════════════════════════════════

/**
 * @brief 保存配置、释放 Renderer/UI/GL/窗口资源
 */
void Kernel::shutdown() {
    MM_LOG_INFO("Kernel", "Shutting down...");

    // 将当前分辨率/全屏状态写回 config.ini
    platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, m_windowWidth);
    platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, m_windowHeight);
    platform::Config::setInt(platform::Config::KEY_FULLSCREEN, m_fullscreen ? 1 : 0);
    platform::Config::save();

    m_uiManager.shutdown();   // 释放 ImGui 资源
    m_renderer.shutdown();    // 释放 OpenGL 资源
    renderer::TextureCache::instance().shutdown();
    m_stateManager = StateManager();  // 重置状态机（触发各状态析构）
    m_eventManager.clear();   // 清空事件订阅

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);  // 销毁 GL 上下文
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);  // 销毁 SDL 窗口
        m_window = nullptr;
    }
    SDL_Quit();  // 关闭 SDL 子系统

    m_initialized = false;
    MM_LOG_INFO("Kernel", "Shutdown complete");
}

/**
 * @brief 设置窗口客户端分辨率并持久化到 config
 */
void Kernel::setResolution(int width, int height) {
    if (!m_window) return;  // 窗口未创建则忽略

    m_windowWidth = width;    // 更新内存中的用户选择
    m_windowHeight = height;

    if (m_fullscreen) {
        // 全屏时仅保存设置，退出全屏时 applyWindowMode 生效
    } else {
        applyWindowMode();  // 窗口模式立即应用新尺寸
    }

    platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, width);
    platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, height);
    platform::Config::save();  // 持久化到磁盘

    m_uiManager.syncUiScale(true);  // 立即按新窗口高度重载字体/样式

    MM_LOG_INFO("Kernel", "Resolution set to " + std::to_string(width) + "x" + std::to_string(height));
}

void Kernel::getCurrentResolution(int& width, int& height) const {
    width = m_windowWidth;   // 输出用户配置的分辨率宽
    height = m_windowHeight;
}

/**
 * @brief 切换全屏；进入时自动使用 NATIVE 分辨率
 */
void Kernel::setFullscreen(bool fullscreen) {
    if (!m_window) return;

    m_fullscreen = fullscreen;
    if (fullscreen) {
        m_windowWidth = m_displayWidth;    // 全屏使用显示器原生宽
        m_windowHeight = m_displayHeight;
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);  // 进入全屏桌面模式

        platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, m_windowWidth);
        platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, m_windowHeight);
    } else {
        SDL_SetWindowFullscreen(m_window, 0);  // 退出全屏
        applyWindowMode();                      // 恢复窗口模式尺寸策略
    }

    platform::Config::setInt(platform::Config::KEY_FULLSCREEN, fullscreen ? 1 : 0);
    platform::Config::save();

    m_uiManager.syncUiScale(true);  // 全屏切换后同步 UI 缩放

    MM_LOG_INFO("Kernel", std::string("Fullscreen: ") + (fullscreen ? "on" : "off"));
}

// ══════════════════════════════════════════════════════
//  SDL 事件泵与全局快捷键
// ══════════════════════════════════════════════════════

/**
 * @brief 轮询 SDL 事件：ImGui 输入、退出、ESC 状态切换、游玩按键
 */
void Kernel::pumpInputEvents() {
    SDL_Event event;  // 单次 poll 取出的 SDL 事件
    while (SDL_PollEvent(&event)) {  // 排空本帧事件队列
        m_uiManager.processEvent(&event);  // 先交给 ImGui 处理输入

        switch (event.type) {
        case SDL_QUIT:
            m_running = false;  // 用户点击关闭或 Alt+F4
            break;
        case SDL_KEYDOWN:
            // 忽略按键 repeat，仅处理首次按下
            if (!event.key.repeat) {
                dispatchGameplayKeyEvent(event.key, true);  // 游玩判定用按下事件
            }
            // ESC 全局状态切换
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                auto current = m_stateManager.currentState();  // 当前活动状态
                if (current == GameState::MainMenu) {
                    m_running = false;  // 主菜单 ESC → 退出程序
                } else if (current == GameState::Playing) {
                    m_stateManager.transitionTo(GameState::Paused);  // 游玩 → 暂停
                } else if (current == GameState::Paused) {
                    m_stateManager.transitionTo(GameState::Playing);  // 暂停 → 恢复游玩
                } else if (current == GameState::SongSelect) {
                    auto* ss = m_stateManager.getStateAs<SongSelectState>(GameState::SongSelect);
                    // 选曲界面若弹窗消费 ESC 则不返回主菜单
                    if (ss && ss->shouldConsumeEscape()) {
                        // SongSelect 消费 ESC（关闭弹窗），不切换状态
                    } else {
                        m_stateManager.transitionTo(GameState::MainMenu);  // 返回主菜单
                    }
                }
            }
            break;
        case SDL_KEYUP:
            dispatchGameplayKeyEvent(event.key, false);  // 游玩判定用释放事件
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                m_running = false;  // 窗口关闭按钮
            } else if (event.window.event == SDL_WINDOWEVENT_RESIZED
                       || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                // 非全屏时跟踪用户拖拽 resize 后的客户端尺寸
                if (!m_fullscreen) {
                    m_windowWidth = event.window.data1;   // 新客户端宽
                    m_windowHeight = event.window.data2;  // 新客户端高
                }
                m_uiManager.syncUiScale(false);  // 窗口尺寸变化时按需重载 UI 缩放
            }
            break;
        default:
            break;  // 其他事件类型忽略
        }
    }
}

} // namespace melody_matrix::core

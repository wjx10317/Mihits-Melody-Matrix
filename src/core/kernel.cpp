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
#include "kernel.h"
#include "game_state.h"
#include "core/states/playing_state.h"
#include "platform/config.h"
#include "core/states/song_select_state.h"
#include "util/logger.h"
#include "util/exceptions.h"

#include <SDL.h>
#include <glad.h>
#include <chrono>
#include <iostream>
#include <algorithm>

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════
//  分辨率列表（设置菜单可选）
// ══════════════════════════════════════════════════════

static std::vector<Resolution> s_resolutions;

/**
 * @brief 构建支持的分辨率列表
 *
 * 首项为显示器 NATIVE 分辨率，其后追加常见分辨率并去重。
 */
static void buildResolutionList() {
    s_resolutions.clear();
    SDL_DisplayMode desktop;
    if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
        s_resolutions.push_back({ desktop.w, desktop.h, "NATIVE" });
    }
    static const Resolution common[] = {
        { 2560, 1600, "2560x1600" },
        { 2560, 1440, "2560x1440" },
        { 1920, 1200, "1920x1200" },
        { 1920, 1080, "1920x1080" },
        { 1680, 1050, "1680x1050" },
        { 1440,  900, "1440x900"  },
        { 1280,  720, "1280x720"  },
    };
    for (const auto& r : common) {
        bool dup = false;
        for (const auto& existing : s_resolutions) {
            if (existing.width == r.width && existing.height == r.height) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            s_resolutions.push_back(r);
        }
    }
}

// ══════════════════════════════════════════════════════
//  SDL 事件 timestamp 扩展（32 位 → 64 位）
// ══════════════════════════════════════════════════════

/**
 * @brief 将 SDL 32 位 event.timestamp 扩展为与 SDL_GetTicks64 对齐的 64 位值
 * @param eventTimestamp SDL 事件中的 Uint32 timestamp
 * @return 扩展后的 64 位 tick，可用于 songTimeAtTickMs
 *
 * SDL 事件 timestamp 会回绕；通过与当前 tick 对齐最近邻消除歧义。
 */
static uint64_t expandSdlEventTimestamp(Uint32 eventTimestamp) {
    const uint64_t now = SDL_GetTicks64();
    const uint64_t base = now & ~0xffffffffULL;
    uint64_t candidate = base | static_cast<uint64_t>(eventTimestamp);
    if (candidate > now + 0x80000000ULL) {
        candidate -= 0x100000000ULL;
    } else if (candidate + 0x80000000ULL < now) {
        candidate += 0x100000000ULL;
    }
    return candidate;
}

// ══════════════════════════════════════════════════════
//  游玩输入与时钟同步
// ══════════════════════════════════════════════════════

/**
 * @brief 将键盘事件分派给 PlayingState，使用事件 timestamp 计算判定歌曲时间
 * @param keyEvent SDL 键盘事件
 * @param pressed 按下或释放
 */
void Kernel::dispatchGameplayKeyEvent(const SDL_KeyboardEvent& keyEvent, bool pressed) {
    if (m_stateManager.currentState() != GameState::Playing) return;
    auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
    if (!playing) return;

    // 在 poll 阶段立即判定，避免等到 240Hz update 造成一帧延迟
    const uint64_t eventTickMs = expandSdlEventTimestamp(keyEvent.timestamp);
    const int64_t eventSongTimeMs = m_clock.songTimeAtTickMs(eventTickMs);
    playing->handleKeyEvent(static_cast<int32_t>(keyEvent.keysym.sym), pressed, eventSongTimeMs);
}

/**
 * @brief Playing 状态下从音频引擎拉取 cursor 并同步 Clock
 */
void Kernel::syncPlayingClock() {
    if (m_stateManager.currentState() != GameState::Playing) return;
    auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
    if (playing) {
        playing->syncClockFromAudio();
    }
}

const std::vector<Resolution>& Kernel::supportedResolutions() {
    if (s_resolutions.empty()) {
        buildResolutionList();
    }
    return s_resolutions;
}

Kernel& Kernel::instance() {
    static Kernel s_instance;
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
    int displayIdx = window ? SDL_GetWindowDisplayIndex(window) : 0;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(displayIdx, &usable) != 0) return;

    int top = 0, left = 0, bottom = 0, right = 0;
    if (window) {
        SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    } else {
        top = 31; left = 8; bottom = 8; right = 8;
    }

    int maxW = usable.w - left - right;
    int maxH = usable.h - top - bottom;
    if (maxW < 640) maxW = 640;
    if (maxH < 480) maxH = 480;

    w = std::min(w, maxW);
    h = std::min(h, maxH);
}

/**
 * @brief 根据当前分辨率应用窗口模式
 *
 * 分辨率 ≥ 显示器原生 → 无边框全屏客户端；否则有边框并 clamp 到可用区域。
 * 与全屏模式独立；全屏时本函数 early return。
 */
void Kernel::applyWindowMode() {
    if (!m_window || m_fullscreen) return;

    bool needsBorderless = (m_windowWidth >= m_displayWidth && m_windowHeight >= m_displayHeight);

    if (needsBorderless) {
        SDL_SetWindowBordered(m_window, SDL_FALSE);
        SDL_SetWindowSize(m_window, m_displayWidth, m_displayHeight);
        SDL_SetWindowPosition(m_window, 0, 0);
    } else {
        SDL_SetWindowBordered(m_window, SDL_TRUE);
        int setW = m_windowWidth, setH = m_windowHeight;
        clampToUsable(m_window, setW, setH);
        SDL_SetWindowSize(m_window, setW, setH);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        MM_LOG_FATAL("Kernel", std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    // ── 检测显示器原生分辨率 ──
    {
        SDL_DisplayMode desktop;
        if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
            m_displayWidth = desktop.w;
            m_displayHeight = desktop.h;
            MM_LOG_INFO("Kernel", "Display native: " +
                std::to_string(desktop.w) + "x" + std::to_string(desktop.h));
        } else {
            MM_LOG_WARN("Kernel", "Cannot detect display resolution, using defaults");
        }
    }

    buildResolutionList();

    // ── 从 config.ini 加载分辨率与全屏设置 ──
    platform::Config::load();
    m_windowWidth = platform::Config::getInt(platform::Config::KEY_RESOLUTION_W, 0);
    m_windowHeight = platform::Config::getInt(platform::Config::KEY_RESOLUTION_H, 0);
    m_fullscreen = platform::Config::getInt(platform::Config::KEY_FULLSCREEN, 0) != 0;

    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        m_windowWidth = 1920;
        m_windowHeight = 1080;
        MM_LOG_INFO("Kernel", "No saved resolution, using default: 1920x1080");
    }

    // ── OpenGL 3.3 Core 属性 ──
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // ── 创建窗口 ──
    int createW = m_windowWidth;
    int createH = m_windowHeight;
    if (!m_fullscreen) {
        clampToUsable(nullptr, createW, createH);
    }

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
                       | SDL_WINDOW_ALLOW_HIGHDPI;
    if (m_fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
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

    SDL_SetWindowMinimumSize(m_window, 800, 600);

    // ── GL 上下文与 GLAD ──
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        MM_LOG_FATAL("Kernel", std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    if (!gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        MM_LOG_FATAL("Kernel", "Failed to initialize GLAD (OpenGL 3.3 Core)");
        SDL_GL_DeleteContext(m_glContext);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    MM_LOG_INFO("Kernel", "OpenGL " + std::to_string(major) + "." + std::to_string(minor));

    if (major < 3 || (major == 3 && minor < 3)) {
        MM_LOG_FATAL("Kernel", "OpenGL 3.3 Core not available.");
        return false;
    }

    SDL_GL_SetSwapInterval(1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.102f, 0.102f, 0.180f, 1.0f); // #1a1a2e

    if (!m_renderer.init()) {
        MM_LOG_WARN("Kernel", "Renderer initialization failed");
    }
    if (!m_uiManager.init(m_window, m_glContext)) {
        MM_LOG_WARN("Kernel", "UI manager initialization failed");
    }

    if (!m_fullscreen) {
        applyWindowMode();
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
 * 每帧：同步时钟 → 处理输入 → 累积 fixed update → 清屏 → 带 visual lead 渲染谱面
 * → ImGui → SwapBuffers。
 */
void Kernel::run() {
    if (!m_initialized) {
        MM_LOG_FATAL("Kernel", "Cannot run: Kernel not initialized");
        return;
    }

    m_running = true;
    constexpr double FIXED_DT = 1.0 / 240.0;
    double accumulator = 0.0;
    auto lastTime = std::chrono::steady_clock::now();

    MM_LOG_INFO("Kernel", "Main loop started");

    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        double frameTime = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        frameTime = std::min(frameTime, 0.25); // 防 spiral of death
        accumulator += frameTime;

        // 先同步音频时钟再处理输入，按键 timestamp 才用到最新 anchor
        syncPlayingClock();
        pumpInputEvents();

        while (accumulator >= FIXED_DT) {
            m_stateManager.update(static_cast<float>(FIXED_DT));
            accumulator -= FIXED_DT;
        }

        // 视口使用 framebuffer 物理像素尺寸
        int drawW = 0, drawH = 0;
        SDL_GL_GetDrawableSize(m_window, &drawW, &drawH);
        glViewport(0, 0, drawW, drawH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 视觉超前：补偿 VSync/扫描延迟，使 note 中心与判定时刻对齐
        const int64_t songNowMs = m_clock.interpolatedNowMs();
        const int64_t configLeadMs = platform::Config::getInt(
            platform::Config::KEY_VISUAL_LEAD, 16);
        const int64_t frameLeadMs = static_cast<int64_t>(frameTime * 500.0 + 0.5);
        const int64_t visualTimeMs = songNowMs + configLeadMs + frameLeadMs;

        m_renderer.renderFrame(visualTimeMs);

        m_uiManager.newFrame();
        m_stateManager.render();
        m_uiManager.renderFrame();

        SDL_GL_SwapWindow(m_window);
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

    platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, m_windowWidth);
    platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, m_windowHeight);
    platform::Config::setInt(platform::Config::KEY_FULLSCREEN, m_fullscreen ? 1 : 0);
    platform::Config::save();

    m_uiManager.shutdown();
    m_renderer.shutdown();
    m_stateManager = StateManager();
    m_eventManager.clear();

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();

    m_initialized = false;
    MM_LOG_INFO("Kernel", "Shutdown complete");
}

/**
 * @brief 设置窗口客户端分辨率并持久化到 config
 */
void Kernel::setResolution(int width, int height) {
    if (!m_window) return;

    m_windowWidth = width;
    m_windowHeight = height;

    if (m_fullscreen) {
        // 全屏时仅保存设置，退出全屏时 applyWindowMode 生效
    } else {
        applyWindowMode();
    }

    platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, width);
    platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, height);
    platform::Config::save();

    MM_LOG_INFO("Kernel", "Resolution set to " + std::to_string(width) + "x" + std::to_string(height));
}

void Kernel::getCurrentResolution(int& width, int& height) const {
    width = m_windowWidth;
    height = m_windowHeight;
}

/**
 * @brief 切换全屏；进入时自动使用 NATIVE 分辨率
 */
void Kernel::setFullscreen(bool fullscreen) {
    if (!m_window) return;

    m_fullscreen = fullscreen;
    if (fullscreen) {
        m_windowWidth = m_displayWidth;
        m_windowHeight = m_displayHeight;
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);

        platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, m_windowWidth);
        platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, m_windowHeight);
    } else {
        SDL_SetWindowFullscreen(m_window, 0);
        applyWindowMode();
    }

    platform::Config::setInt(platform::Config::KEY_FULLSCREEN, fullscreen ? 1 : 0);
    platform::Config::save();

    MM_LOG_INFO("Kernel", std::string("Fullscreen: ") + (fullscreen ? "on" : "off"));
}

// ══════════════════════════════════════════════════════
//  SDL 事件泵与全局快捷键
// ══════════════════════════════════════════════════════

/**
 * @brief 轮询 SDL 事件：ImGui 输入、退出、ESC 状态切换、游玩按键
 */
void Kernel::pumpInputEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        m_uiManager.processEvent(&event);

        switch (event.type) {
        case SDL_QUIT:
            m_running = false;
            break;
        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                dispatchGameplayKeyEvent(event.key, true);
            }
            // ESC 全局状态切换
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                auto current = m_stateManager.currentState();
                if (current == GameState::MainMenu) {
                    m_running = false;
                } else if (current == GameState::Playing) {
                    m_stateManager.transitionTo(GameState::Paused);
                } else if (current == GameState::Paused) {
                    m_stateManager.transitionTo(GameState::Playing);
                } else if (current == GameState::SongSelect) {
                    auto* ss = m_stateManager.getStateAs<SongSelectState>(GameState::SongSelect);
                    if (ss && ss->shouldConsumeEscape()) {
                        // SongSelect 消费 ESC（关闭弹窗），不切换状态
                    } else {
                        m_stateManager.transitionTo(GameState::MainMenu);
                    }
                }
            }
            break;
        case SDL_KEYUP:
            dispatchGameplayKeyEvent(event.key, false);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                m_running = false;
            } else if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                if (!m_fullscreen) {
                    m_windowWidth = event.window.data1;
                    m_windowHeight = event.window.data2;
                }
            }
            break;
        default:
            break;
        }
    }
}

} // namespace melody_matrix::core

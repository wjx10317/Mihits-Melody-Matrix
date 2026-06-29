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

// ── Supported resolutions (user-selectable in settings) ──
static std::vector<Resolution> s_resolutions;

static void buildResolutionList() {
    s_resolutions.clear();
    // NATIVE = display's physical pixel resolution
    SDL_DisplayMode desktop;
    if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
        s_resolutions.push_back({ desktop.w, desktop.h, "NATIVE" });
    }
    // Common resolutions (physical pixels, sorted largest first)
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

void Kernel::dispatchGameplayKeyEvent(const SDL_KeyboardEvent& keyEvent, bool pressed) {
    if (m_stateManager.currentState() != GameState::Playing) return;
    auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
    if (!playing) return;

    // 使用 SDL 事件 timestamp 换算歌曲时间，避免等到 update() 才判定造成帧延迟。
    const uint64_t eventTickMs = expandSdlEventTimestamp(keyEvent.timestamp);
    const int64_t eventSongTimeMs = m_clock.songTimeAtTickMs(eventTickMs);
    playing->handleKeyEvent(static_cast<int32_t>(keyEvent.keysym.sym), pressed, eventSongTimeMs);
}

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

/// Helper: clamp client size (physical pixels) to fit within usable display area.
/// Used for bordered windows where decorations add to the total window size.
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

/// Apply the correct window mode based on current resolution:
/// - Resolution >= display native → borderless window (client area = full screen)
/// - Resolution < display native  → bordered window (client area = resolution, clamped)
/// This is independent from fullscreen mode.
void Kernel::applyWindowMode() {
    if (!m_window || m_fullscreen) return;

    bool needsBorderless = (m_windowWidth >= m_displayWidth && m_windowHeight >= m_displayHeight);

    if (needsBorderless) {
        // Borderless: no decorations, client area = full screen
        SDL_SetWindowBordered(m_window, SDL_FALSE);
        SDL_SetWindowSize(m_window, m_displayWidth, m_displayHeight);
        SDL_SetWindowPosition(m_window, 0, 0);
    } else {
        // Bordered: client area = m_windowWidth × m_windowHeight, clamped to fit
        SDL_SetWindowBordered(m_window, SDL_TRUE);
        int setW = m_windowWidth, setH = m_windowHeight;
        clampToUsable(m_window, setW, setH);
        SDL_SetWindowSize(m_window, setW, setH);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    MM_LOG_INFO("Kernel", "Window mode: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight)
        + (needsBorderless ? " (borderless)" : " (bordered)"));
}

bool Kernel::init(const std::string& title, int /*width*/, int /*height*/) {
    MM_LOG_INFO("Kernel", "Initializing Melody Matrix...");

    // ── Set Per-Monitor V2 DPI awareness BEFORE SDL_Init ──
    // This makes all SDL coordinate APIs return physical pixels directly,
    // eliminating the need for any logical/physical pixel conversion.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        MM_LOG_FATAL("Kernel", std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    // ── Detect display resolution (physical pixels) ──
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

    // ── Load config for saved resolution (physical pixels) ──
    platform::Config::load();
    m_windowWidth = platform::Config::getInt(platform::Config::KEY_RESOLUTION_W, 0);
    m_windowHeight = platform::Config::getInt(platform::Config::KEY_RESOLUTION_H, 0);
    m_fullscreen = platform::Config::getInt(platform::Config::KEY_FULLSCREEN, 0) != 0;

    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        m_windowWidth = 1920;
        m_windowHeight = 1080;
        MM_LOG_INFO("Kernel", "No saved resolution, using default: 1920x1080");
    }

    // ── OpenGL attributes ──
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // ── Create window ──
    // Initial size is clamped; applyWindowMode() will fix it after GL context setup.
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

    // 设置最小窗口尺寸，防止布局过小导致内容溢出
    SDL_SetWindowMinimumSize(m_window, 800, 600);

    // ── Create GL context ──
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

    // ── Apply correct window mode (borderless for native, bordered for others) ──
    if (!m_fullscreen) {
        applyWindowMode();
    }

    m_initialized = true;
    return true;
}

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
        frameTime = std::min(frameTime, 0.25);
        accumulator += frameTime;

        // 先同步音频时钟再处理输入，避免按键 timestamp 用到上一帧的 anchor。
        syncPlayingClock();
        pumpInputEvents();

        while (accumulator >= FIXED_DT) {
            m_stateManager.update(static_cast<float>(FIXED_DT));
            accumulator -= FIXED_DT;
        }

        // glViewport uses the framebuffer size (physical pixels)
        int drawW = 0, drawH = 0;
        SDL_GL_GetDrawableSize(m_window, &drawW, &drawH);
        glViewport(0, 0, drawW, drawH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 渲染时间超前：补偿 VSync/扫描延迟，使玩家看到的 note 中心与判定时刻对齐。
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

void Kernel::setResolution(int width, int height) {
    if (!m_window) return;

    // Store the desired client area size
    m_windowWidth = width;
    m_windowHeight = height;

    if (m_fullscreen) {
        // In fullscreen mode, just save the setting.
        // applyWindowMode() will be called when exiting fullscreen.
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

void Kernel::setFullscreen(bool fullscreen) {
    if (!m_window) return;

    m_fullscreen = fullscreen;
    if (fullscreen) {
        // Entering fullscreen → auto-switch to NATIVE resolution
        m_windowWidth = m_displayWidth;
        m_windowHeight = m_displayHeight;
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);

        platform::Config::setInt(platform::Config::KEY_RESOLUTION_W, m_windowWidth);
        platform::Config::setInt(platform::Config::KEY_RESOLUTION_H, m_windowHeight);
    } else {
        // Exiting fullscreen → apply window mode (NATIVE becomes borderless, others bordered)
        SDL_SetWindowFullscreen(m_window, 0);
        applyWindowMode();
    }

    platform::Config::setInt(platform::Config::KEY_FULLSCREEN, fullscreen ? 1 : 0);
    platform::Config::save();

    MM_LOG_INFO("Kernel", std::string("Fullscreen: ") + (fullscreen ? "on" : "off"));
}

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
            // ESC 特殊处理：状态转换
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
                        // ESC 由 SongSelect 消费（关闭弹窗），不执行状态转换
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

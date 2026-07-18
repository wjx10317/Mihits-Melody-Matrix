/**
 * @file raw_input_win.cpp
 * @brief Windows Raw Input 游玩按键适配器
 *
 * WM_INPUT 必须在窗口过程内同步 GetRawInputData（不可经 SDL 事件队列延后）。
 * 子类化 HWND，收包瞬间打 HostClock，再在 poll() 中交给判定出口。
 * 不使用 RIDEV_NOLEGACY，以免打断 SDL/ImGui 键盘路径。
 */
#ifdef _WIN32

#include "input/gameplay_input.h"
#include "time/host_clock.h"
#include "util/logger.h"

#include <SDL.h>
#include <SDL_syswm.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace melody_matrix::input {
namespace {

int32_t virtualKeyToSdlKey(USHORT vkey) {
    if (vkey >= 'A' && vkey <= 'Z') {
        return static_cast<int32_t>(vkey - 'A' + 'a');
    }
    return 0;
}

class RawInputAdapter final : public IGameplayInput {
public:
    bool init(SDL_Window* window) override {
        if (!window) return false;
        if (s_active) {
            MM_LOG_WARN("Input", "Raw init: another adapter already active");
            return false;
        }

        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) {
            MM_LOG_WARN("Input", std::string("Raw init: SDL_GetWindowWMInfo failed: ") + SDL_GetError());
            return false;
        }
        if (info.subsystem != SDL_SYSWM_WINDOWS || !info.info.win.window) {
            MM_LOG_WARN("Input", "Raw init: invalid Windows HWND");
            return false;
        }

        m_hwnd = info.info.win.window;

        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x06;
        // INPUTSINK：非前台也能收（自动化验证 / 失焦边缘）；不设 NOLEGACY
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = m_hwnd;

        if (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE) {
            MM_LOG_WARN("Input", "Raw init: RegisterRawInputDevices failed");
            m_hwnd = nullptr;
            return false;
        }

        m_prevProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
        if (!m_prevProc) {
            MM_LOG_WARN("Input", "Raw init: SetWindowLongPtr subclass failed");
            RAWINPUTDEVICE remove{};
            remove.usUsagePage = 0x01;
            remove.usUsage = 0x06;
            remove.dwFlags = RIDEV_REMOVE;
            remove.hwndTarget = nullptr;
            RegisterRawInputDevices(&remove, 1, sizeof(remove));
            m_hwnd = nullptr;
            return false;
        }

        m_registered = true;
        s_active = this;
        std::memset(m_down, 0, sizeof(m_down));
        std::memset(m_lastRawHostMs, 0, sizeof(m_lastRawHostMs));
        std::memset(m_hasRawStamp, 0, sizeof(m_hasRawStamp));
        m_pending.clear();
        return true;
    }

    void shutdown() override {
        if (m_hwnd && m_prevProc) {
            SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_prevProc));
            m_prevProc = nullptr;
        }
        if (m_registered) {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage = 0x06;
            rid.dwFlags = RIDEV_REMOVE;
            rid.hwndTarget = nullptr;
            RegisterRawInputDevices(&rid, 1, sizeof(rid));
        }
        if (s_active == this) s_active = nullptr;
        m_registered = false;
        m_hwnd = nullptr;
        m_pending.clear();
    }

    void poll(const GameplayKeySink& sink) override {
        if (!sink || m_pending.empty()) return;
        std::vector<Pending> batch;
        batch.swap(m_pending);
        for (const auto& p : batch) {
            sink(p.sdlKey, p.pressed, p.hostMs);
        }
    }

    void onSdlEvent(const SDL_Event& event, const GameplayKeySink& /*sink*/) override {
        // 判定键已由 WndProc → poll 产出；此处仅做 raw→poll 延迟诊断
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
            observePollLag(static_cast<int32_t>(event.key.keysym.sym));
        }
    }

    const char* name() const override { return "raw"; }

private:
    struct Pending {
        int32_t sdlKey;
        bool pressed;
        int64_t hostMs;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RawInputAdapter* self = s_active;
        if (self && msg == WM_INPUT) {
            self->onWmInput(lParam);
        }
        if (self && self->m_prevProc) {
            return CallWindowProcW(self->m_prevProc, hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void onWmInput(LPARAM lParam) {
        const HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(lParam);
        UINT size = 0;
        if (GetRawInputData(hRaw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)
            || size == 0 || size > 128) {
            return;
        }
        alignas(RAWINPUT) BYTE stackBuf[128];
        if (GetRawInputData(hRaw, RID_INPUT, stackBuf, &size, sizeof(RAWINPUTHEADER)) != size) {
            return;
        }

        const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(stackBuf);
        if (raw->header.dwType != RIM_TYPEKEYBOARD) return;

        const RAWKEYBOARD& kb = raw->data.keyboard;
        if (kb.VKey >= 0xFF) return;

        const bool pressed = (kb.Flags & RI_KEY_BREAK) == 0;
        const int32_t sdlKey = virtualKeyToSdlKey(kb.VKey);
        if (sdlKey == 0) return;

        const int idx = sdlKey - 'a';
        if (idx < 0 || idx >= 26) return;

        if (pressed) {
            if (m_down[idx]) return;
            m_down[idx] = true;
        } else {
            if (!m_down[idx]) return;
            m_down[idx] = false;
        }

        const int64_t hostMs = time::HostClock::nowMs();
        if (pressed) {
            m_lastRawHostMs[idx] = hostMs;
            m_hasRawStamp[idx] = true;
        }
        m_pending.push_back({sdlKey, pressed, hostMs});
    }

    void observePollLag(int32_t sdlKey) {
        const int idx = sdlKey - 'a';
        if (idx < 0 || idx >= 26 || !m_hasRawStamp[idx]) return;
        const int64_t pollHost = time::HostClock::nowMs();
        const int64_t lag = pollHost - m_lastRawHostMs[idx];
        m_hasRawStamp[idx] = false;

        m_lagSum += static_cast<double>(lag);
        m_lagAbsSum += std::abs(static_cast<double>(lag));
        m_lagMax = std::max(m_lagMax, lag);
        m_lagMin = std::min(m_lagMin, lag);
        ++m_lagCount;

        if (m_lagCount >= 60) {
            const double mean = m_lagSum / static_cast<double>(m_lagCount);
            const double absMean = m_lagAbsSum / static_cast<double>(m_lagCount);
            MM_LOG_INFO("Input",
                "KeySample raw→poll lag: n=" + std::to_string(m_lagCount)
                + " mean=" + std::to_string(mean)
                + " absMean=" + std::to_string(absMean)
                + " min=" + std::to_string(m_lagMin)
                + " max=" + std::to_string(m_lagMax) + " ms");
            m_lagSum = 0;
            m_lagAbsSum = 0;
            m_lagMax = INT64_MIN;
            m_lagMin = INT64_MAX;
            m_lagCount = 0;
        }
    }

    static RawInputAdapter* s_active;

    HWND m_hwnd = nullptr;
    WNDPROC m_prevProc = nullptr;
    bool m_registered = false;
    bool m_down[26]{};
    int64_t m_lastRawHostMs[26]{};
    bool m_hasRawStamp[26]{};
    std::vector<Pending> m_pending;

    double m_lagSum = 0;
    double m_lagAbsSum = 0;
    int64_t m_lagMax = INT64_MIN;
    int64_t m_lagMin = INT64_MAX;
    int m_lagCount = 0;
};

RawInputAdapter* RawInputAdapter::s_active = nullptr;

} // namespace

std::unique_ptr<IGameplayInput> tryMakeRawInputAdapter() {
    return std::make_unique<RawInputAdapter>();
}

} // namespace melody_matrix::input

#endif // _WIN32

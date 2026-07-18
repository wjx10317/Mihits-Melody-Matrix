/**
 * @file wasapi_playhead_win.cpp
 * @brief WasapiPlayhead：IAudioClock / IAudioClock2 + 跨 period 的 QPC 外推
 *
 * 日志事实（2026-07-18 Debug）：ph-cu≈0，但 win max=+9、phStepMax≈10、outliers≈70/120。
 * 根因：GetDevicePosition 的硬件计数约每 10ms（共享 period）才跳变一次；
 * 仅对「本次调用返回的 QPC」做微秒级外推，填不满 period 空隙 → playhead 仍 10ms 台阶，
 * HostClock 在台阶之间往前跑 → song-ph 锯齿到 +9。
 *
 * 修法：
 *   1) 硬件 pos 变化时才刷新锚点；期间墙钟外推填平 period。
 *   2) 硬件落后于预测时不换锚；输出位置单调不减，避免超前后被拉回造成 song-ph 尖峰。
 */
#ifdef _WIN32

#include "audio/wasapi_playhead.h"

#include "audio/audio_engine.h"
#include "util/logger.h"

#include <miniaudio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>

#include <cstdint>

namespace melody_matrix::audio {
namespace {

LARGE_INTEGER qpcFrequency() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return freq;
}

UINT64 qpcNowAsHns() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const UINT64 ticks = static_cast<UINT64>(counter.QuadPart);
    const UINT64 freq = static_cast<UINT64>(qpcFrequency().QuadPart);
    if (freq == 0) return 0;
    return (ticks * 10000000ULL) / freq;
}

UINT64 qpcNowRaw() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<UINT64>(counter.QuadPart);
}

class WasapiPlayhead final : public time::IAudioPlayhead {
public:
    WasapiPlayhead(IAudioClock* clock, IAudioClock2* clock2, UINT64 freq, int32_t sampleRate)
        : m_clock(clock)
        , m_clock2(clock2)
        , m_freq(freq)
        , m_sampleRate(sampleRate)
        , m_useFrames(clock2 != nullptr) {
        noteTimelineEpoch(0);
    }

    ~WasapiPlayhead() override {
        if (m_clock2) {
            m_clock2->Release();
            m_clock2 = nullptr;
        }
        if (m_clock) {
            m_clock->Release();
            m_clock = nullptr;
        }
    }

    WasapiPlayhead(const WasapiPlayhead&) = delete;
    WasapiPlayhead& operator=(const WasapiPlayhead&) = delete;

    void noteTimelineEpoch(int64_t songMs) override {
        m_hwAnchorValid = false;
        m_haveEmitted = false;
        m_lastEmittedPos = 0;
        m_songMsAtEpoch = songMs;
        Sample s = readSample();
        m_devicePosAtEpoch = s.pos;
        m_hasEpoch = s.ok;
    }

    int64_t positionFrames() const override {
        if (m_useFrames) {
            const Sample s = readSample();
            return s.ok ? static_cast<int64_t>(s.pos) : 0;
        }
        const int32_t sr = sampleRate();
        if (sr <= 0) return 0;
        return positionMs() * static_cast<int64_t>(sr) / 1000;
    }

    int32_t sampleRate() const override { return m_sampleRate; }

    int64_t positionMs() const override {
        if (!m_hasEpoch) return 0;
        const Sample s = readSample();
        if (!s.ok) return m_songMsAtEpoch;

        const UINT64 pos = s.pos;
        const UINT64 delta = (pos >= m_devicePosAtEpoch) ? (pos - m_devicePosAtEpoch) : 0;

        int64_t deltaMs = 0;
        if (m_useFrames) {
            const int32_t srSafe = m_sampleRate > 0 ? m_sampleRate : 1;
            const UINT64 sr = static_cast<UINT64>(srSafe);
            deltaMs = static_cast<int64_t>((delta * 1000ULL) / sr);
        } else {
            if (m_freq == 0) return m_songMsAtEpoch;
            deltaMs = static_cast<int64_t>(
                (delta / m_freq) * 1000ULL + ((delta % m_freq) * 1000ULL) / m_freq);
        }
        return m_songMsAtEpoch + deltaMs;
    }

    const char* backendName() const override {
        return m_useFrames ? "wasapi-clock2" : "wasapi";
    }

private:
    struct Sample {
        UINT64 pos = 0;
        bool ok = false;
    };

    Sample readSample() const {
        if (m_useFrames) return readSampleClock2();
        return readSampleClock1();
    }

    /// 硬件 pos 变化才换锚；期间墙钟外推。禁止外推超前后被硬件拉回（否则 song-ph 尖峰）。
    Sample extrapolateFromHwAnchor(UINT64 hwPos, UINT64 hwQpc, UINT64 qpcNow,
                                   UINT64 qpcUnitsPerSec, UINT64 rateUnitsPerSec) const {
        Sample out;

        auto advance = [&](UINT64 basePos, UINT64 baseQpc) -> UINT64 {
            UINT64 p = basePos;
            if (baseQpc > 0 && qpcNow > baseQpc && qpcUnitsPerSec > 0 && rateUnitsPerSec > 0) {
                p += ((qpcNow - baseQpc) * rateUnitsPerSec) / qpcUnitsPerSec;
            }
            return p;
        };

        const UINT64 fromHw = advance(hwPos, hwQpc);

        if (!m_hwAnchorValid) {
            m_hwAnchorPos = hwPos;
            m_hwAnchorQpc = hwQpc;
            m_hwAnchorValid = true;
            m_lastEmittedPos = fromHw;
            m_haveEmitted = true;
            out.pos = fromHw;
            out.ok = true;
            return out;
        }

        const UINT64 predicted = advance(m_hwAnchorPos, m_hwAnchorQpc);
        // 约 1ms 容差：硬件追上预测才换锚，避免外推超前后硬拉回
        const UINT64 slack = (rateUnitsPerSec > 1000) ? (rateUnitsPerSec / 1000) : 1;

        if (hwPos != m_hwAnchorPos) {
            if (fromHw + slack >= predicted) {
                m_hwAnchorPos = hwPos;
                m_hwAnchorQpc = hwQpc;
            }
            // else：硬件仍落后于预测 → 继续沿用旧锚自由推进
        }

        UINT64 pos = advance(m_hwAnchorPos, m_hwAnchorQpc);
        if (fromHw > pos) {
            pos = fromHw;
            if (hwPos > m_hwAnchorPos) {
                m_hwAnchorPos = hwPos;
                m_hwAnchorQpc = hwQpc;
                pos = advance(m_hwAnchorPos, m_hwAnchorQpc);
            }
        }

        if (m_haveEmitted && pos < m_lastEmittedPos) {
            pos = m_lastEmittedPos;
        } else {
            m_lastEmittedPos = pos;
            m_haveEmitted = true;
        }

        out.pos = pos;
        out.ok = true;
        return out;
    }

    Sample readSampleClock2() const {
        if (!m_clock2) return {};

        UINT64 deviceFrames = 0;
        UINT64 qpcAtSample = 0;
        if (FAILED(m_clock2->GetDevicePosition(&deviceFrames, &qpcAtSample))) {
            return {};
        }

        const int32_t srSafe = m_sampleRate > 0 ? m_sampleRate : 1;
        const UINT64 sr = static_cast<UINT64>(srSafe);
        return extrapolateFromHwAnchor(deviceFrames, qpcAtSample, qpcNowRaw(),
                                       static_cast<UINT64>(qpcFrequency().QuadPart), sr);
    }

    Sample readSampleClock1() const {
        if (!m_clock || m_freq == 0) return {};

        UINT64 pos = 0;
        UINT64 qpcHns = 0;
        if (FAILED(m_clock->GetPosition(&pos, &qpcHns))) {
            return {};
        }

        return extrapolateFromHwAnchor(pos, qpcHns, qpcNowAsHns(), 10000000ULL, m_freq);
    }

    IAudioClock* m_clock = nullptr;
    IAudioClock2* m_clock2 = nullptr;
    UINT64 m_freq = 0;
    int32_t m_sampleRate = 48000;
    bool m_useFrames = false;
    int64_t m_songMsAtEpoch = 0;
    UINT64 m_devicePosAtEpoch = 0;
    bool m_hasEpoch = false;

    // 跨 period 外推锚点（mutable：const 读路径需要更新）
    mutable bool m_hwAnchorValid = false;
    mutable UINT64 m_hwAnchorPos = 0;
    mutable UINT64 m_hwAnchorQpc = 0;
    mutable bool m_haveEmitted = false;
    mutable UINT64 m_lastEmittedPos = 0;
};

} // namespace

std::unique_ptr<time::IAudioPlayhead> tryMakeWasapiPlayhead(AudioEngine& engine) {
    ma_engine* eng = engine.rawEngine();
    if (!eng) {
        MM_LOG_WARN("Audio", "WasapiPlayhead: no ma_engine");
        return nullptr;
    }

    ma_device* device = ma_engine_get_device(eng);
    if (!device || !device->pContext) {
        MM_LOG_WARN("Audio", "WasapiPlayhead: no ma_device");
        return nullptr;
    }

    if (device->pContext->backend != ma_backend_wasapi) {
        MM_LOG_INFO("Audio", "WasapiPlayhead: backend is not WASAPI (%s)",
                    ma_get_backend_name(device->pContext->backend));
        return nullptr;
    }

    IAudioClient* client = static_cast<IAudioClient*>(device->wasapi.pAudioClientPlayback);
    if (!client) {
        MM_LOG_WARN("Audio", "WasapiPlayhead: pAudioClientPlayback is null");
        return nullptr;
    }

    IAudioClock* clock = nullptr;
    HRESULT hr = client->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&clock));
    if (FAILED(hr) || !clock) {
        MM_LOG_WARN("Audio", "WasapiPlayhead: GetService(IAudioClock) failed hr=0x%08lx",
                    static_cast<unsigned long>(hr));
        return nullptr;
    }

    UINT64 freq = 0;
    hr = clock->GetFrequency(&freq);
    if (FAILED(hr) || freq == 0) {
        MM_LOG_WARN("Audio", "WasapiPlayhead: GetFrequency failed");
        clock->Release();
        return nullptr;
    }

    IAudioClock2* clock2 = nullptr;
    hr = clock->QueryInterface(__uuidof(IAudioClock2), reinterpret_cast<void**>(&clock2));
    if (FAILED(hr) || !clock2) {
        clock2 = nullptr;
        MM_LOG_INFO("Audio", "WasapiPlayhead: IAudioClock2 unavailable, using GetPosition+QPC(hns)");
    }

    int32_t sampleRate = static_cast<int32_t>(ma_engine_get_sample_rate(eng));
    if (sampleRate <= 0) sampleRate = 48000;

    REFERENCE_TIME defaultPeriod = 0;
    REFERENCE_TIME minPeriod = 0;
    if (SUCCEEDED(client->GetDevicePeriod(&defaultPeriod, &minPeriod))) {
        MM_LOG_INFO("Audio",
                    "WasapiPlayhead: devicePeriod default=%.2fms min=%.2fms freq=%llu sr=%d clock2=%s",
                    defaultPeriod / 10000.0, minPeriod / 10000.0,
                    static_cast<unsigned long long>(freq), sampleRate,
                    clock2 ? "yes" : "no");
    } else {
        MM_LOG_INFO("Audio", "WasapiPlayhead: bound IAudioClock freq=%llu sampleRate=%d clock2=%s",
                    static_cast<unsigned long long>(freq), sampleRate,
                    clock2 ? "yes" : "no");
    }

    return std::make_unique<WasapiPlayhead>(clock, clock2, freq, sampleRate);
}

} // namespace melody_matrix::audio

#endif // _WIN32

#include "clock.h"

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    m_audioTimeMs.store(audioFrameTimeMs, std::memory_order_release);
    if (!m_paused) {
        m_anchor = std::chrono::steady_clock::now();
        m_anchorAudioMs = audioFrameTimeMs;
    }
}

int64_t Clock::nowMs() const {
    return m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
}

int64_t Clock::interpolatedNowMs() const {
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    auto elapsed = std::chrono::steady_clock::now() - m_anchor;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return m_anchorAudioMs + elapsedMs + m_userOffsetMs;
}

void Clock::setUserOffset(int64_t offsetMs) {
    m_userOffsetMs = offsetMs;
}

void Clock::reset() {
    m_audioTimeMs.store(0, std::memory_order_release);
    m_anchor = std::chrono::steady_clock::now();
    m_anchorAudioMs = 0;
    m_paused = false;
}

void Clock::pause() {
    m_paused = true;
}

void Clock::resume() {
    m_anchor = std::chrono::steady_clock::now();
    m_anchorAudioMs = m_audioTimeMs.load(std::memory_order_acquire);
    m_paused = false;
}

} // namespace melody_matrix::core

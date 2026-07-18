/**
 * @file clock.cpp
 * @brief Clock 类实现（音频锚点 + HostClock 外推）
 */
#include "clock.h"

#include "time/host_clock.h"

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    // 仅在音频位置实际前进时才刷新 anchor，避免停滞 cursor 拖慢插值。
    int64_t prev = m_audioTimeMs.exchange(audioFrameTimeMs, std::memory_order_acq_rel);
    if (!m_paused && audioFrameTimeMs != prev) {
        m_anchorAudioMs = audioFrameTimeMs;
        m_anchorHostMs = time::HostClock::nowMs();
    }
}

int64_t Clock::elapsedMsSinceAnchor() const {
    return time::HostClock::nowMs() - m_anchorHostMs;
}

int64_t Clock::nowMs() const {
    return m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
}

int64_t Clock::interpolatedNowMs() const {
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    const int64_t freeRun = m_anchorAudioMs + elapsedMsSinceAnchor() + m_userOffsetMs;
    // 限制相对最后一次音频样本的超前量，避免 playhead 短暂停滞时 song-ph 飙到数十 ms
    const int64_t audioNow = m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
    constexpr int64_t kMaxAheadMs = 2;
    if (freeRun > audioNow + kMaxAheadMs) {
        return audioNow + kMaxAheadMs;
    }
    return freeRun;
}

int64_t Clock::songTimeAtHostMs(int64_t hostMs) const {
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    const int64_t freeRun = m_anchorAudioMs + (hostMs - m_anchorHostMs) + m_userOffsetMs;
    const int64_t audioNow = m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
    constexpr int64_t kMaxAheadMs = 2;
    if (freeRun > audioNow + kMaxAheadMs) {
        return audioNow + kMaxAheadMs;
    }
    return freeRun;
}

void Clock::setUserOffset(int64_t offsetMs) {
    m_userOffsetMs = offsetMs;
}

void Clock::reset() {
    m_audioTimeMs.store(0, std::memory_order_release);
    m_anchorAudioMs = 0;
    m_anchorHostMs = time::HostClock::nowMs();
    m_paused = false;
}

void Clock::pause() {
    m_paused = true;
}

void Clock::resume() {
    m_anchorAudioMs = m_audioTimeMs.load(std::memory_order_acquire);
    m_anchorHostMs = time::HostClock::nowMs();
    m_paused = false;
}

} // namespace melody_matrix::core

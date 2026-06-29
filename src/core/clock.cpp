#include "clock.h"

#include <SDL.h>

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    // exchange 返回旧值；仅在音频位置实际前进时才刷新 anchor。
    // 音频 cursor 按设备周期更新（~10ms 一次），但本函数在 240Hz update() 中
    // 被调用。若每次都用停滞的 cursor 重置 anchor，插值会被反复拽回到停滞值，
    // 导致时钟落后实际音频位置约半个周期（~5ms）。仅在前进时更新可消除该滞后。
    int64_t prev = m_audioTimeMs.exchange(audioFrameTimeMs, std::memory_order_acq_rel);
    if (!m_paused && audioFrameTimeMs != prev) {
        m_anchorAudioMs = audioFrameTimeMs;
        m_anchorTickMs = SDL_GetTicks64();
    }
}

int64_t Clock::elapsedMsSinceAnchor() const {
    const int64_t tickNow = static_cast<int64_t>(SDL_GetTicks64());
    return tickNow - static_cast<int64_t>(m_anchorTickMs);
}

int64_t Clock::nowMs() const {
    return m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
}

int64_t Clock::interpolatedNowMs() const {
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    return m_anchorAudioMs + elapsedMsSinceAnchor() + m_userOffsetMs;
}

int64_t Clock::songTimeAtTickMs(uint64_t tickMs) const {
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    const int64_t elapsedMs = static_cast<int64_t>(tickMs) - static_cast<int64_t>(m_anchorTickMs);
    return m_anchorAudioMs + elapsedMs + m_userOffsetMs;
}

void Clock::setUserOffset(int64_t offsetMs) {
    m_userOffsetMs = offsetMs;
}

void Clock::reset() {
    m_audioTimeMs.store(0, std::memory_order_release);
    m_anchorAudioMs = 0;
    m_anchorTickMs = SDL_GetTicks64();
    m_paused = false;
}

void Clock::pause() {
    m_paused = true;
}

void Clock::resume() {
    m_anchorAudioMs = m_audioTimeMs.load(std::memory_order_acquire);
    m_anchorTickMs = SDL_GetTicks64();
    m_paused = false;
}

} // namespace melody_matrix::core

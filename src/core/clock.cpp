#include "clock.h"

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    // exchange 返回旧值；仅在音频位置实际前进时才刷新 anchor。
    // 音频 cursor 按设备周期更新（~10ms 一次），但本函数在 240Hz update() 中
    // 被调用。若每次都用停滞的 cursor 重置 anchor，插值会被反复拽回到停滞值，
    // 导致时钟落后实际音频位置约半个周期（~5ms）。仅在前进时更新可消除该滞后。
    int64_t prev = m_audioTimeMs.exchange(audioFrameTimeMs, std::memory_order_acq_rel);
    if (!m_paused && audioFrameTimeMs != prev) {
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
    // 微秒精度 + 四舍五入，避免 duration_cast<milliseconds> 截断造成最多 ~1ms 落后。
    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    return m_anchorAudioMs + (elapsedUs + 500) / 1000 + m_userOffsetMs;
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

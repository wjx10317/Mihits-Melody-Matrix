/**
 * @file clock.cpp
 * @brief Clock 类实现
 *
 * 文件职责：
 *   实现音频驱动 + SDL tick 插值的歌曲时间计算逻辑。
 *
 * 主要依赖：
 *   clock.h、SDL.h（SDL_GetTicks64）。
 *
 * 在项目中的用法：
 *   由 Kernel 与 PlayingState 间接调用，业务代码通过 kernel.clock() 访问。
 */
#include "clock.h"

#include <SDL.h>

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    // exchange 返回旧值；仅在音频位置实际前进时才刷新 anchor。
    // 音频 cursor 约每 10ms 更新一次，但本函数在 240Hz update 中被调用。
    // 若每次都用停滞 cursor 重置 anchor，插值会被拽回停滞值，
    // 导致时钟落后实际音频约半个周期（~5ms）。
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
    // 恢复时用当前音频时间重新锚定，避免暂停期间 tick 累积
    m_anchorAudioMs = m_audioTimeMs.load(std::memory_order_acquire);
    m_anchorTickMs = SDL_GetTicks64();
    m_paused = false;
}

} // namespace melody_matrix::core

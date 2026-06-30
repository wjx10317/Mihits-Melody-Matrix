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
#include "clock.h"  // Clock 类声明

#include <SDL.h>  // SDL_GetTicks64：毫秒级单调时钟，用于插值外推

namespace melody_matrix::core {

void Clock::syncFromAudio(int64_t audioFrameTimeMs) {
    // exchange 返回旧值；仅在音频位置实际前进时才刷新 anchor。
    // 音频 cursor 约每 10ms 更新一次，但本函数在 240Hz update 中被调用。
    // 若每次都用停滞 cursor 重置 anchor，插值会被拽回停滞值，
    // 导致时钟落后实际音频约半个周期（~5ms）。
    int64_t prev = m_audioTimeMs.exchange(audioFrameTimeMs, std::memory_order_acq_rel);  // 原子写入并取旧 cursor
    // 未暂停且 cursor 实际前进时才刷新插值锚点，避免停滞值拖慢时钟
    if (!m_paused && audioFrameTimeMs != prev) {
        m_anchorAudioMs = audioFrameTimeMs;           // 记录锚点对应的歌曲时间
        m_anchorTickMs = SDL_GetTicks64();            // 记录锚点对应的 SDL 毫秒 tick
    }
}

int64_t Clock::elapsedMsSinceAnchor() const {
    const int64_t tickNow = static_cast<int64_t>(SDL_GetTicks64());  // 当前 SDL 毫秒 tick
    return tickNow - static_cast<int64_t>(m_anchorTickMs);           // 与锚点 tick 的差值
}

int64_t Clock::nowMs() const {
    // 读取最新音频 cursor 并叠加用户偏移（不含 tick 插值）
    return m_audioTimeMs.load(std::memory_order_acquire) + m_userOffsetMs;
}

int64_t Clock::interpolatedNowMs() const {
    // 暂停时冻结在锚点歌曲时间
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    // 锚点歌曲时间 + 自锚点以来 SDL 经过时间 + 用户偏移
    return m_anchorAudioMs + elapsedMsSinceAnchor() + m_userOffsetMs;
}

int64_t Clock::songTimeAtTickMs(uint64_t tickMs) const {
    // 暂停时任意 tick 均映射到冻结的锚点时间（用于按键判定）
    if (m_paused) {
        return m_anchorAudioMs + m_userOffsetMs;
    }
    const int64_t elapsedMs = static_cast<int64_t>(tickMs) - static_cast<int64_t>(m_anchorTickMs);  // 事件 tick 相对锚点偏移
    return m_anchorAudioMs + elapsedMs + m_userOffsetMs;  // 反推该 tick 时刻的歌曲时间
}

void Clock::setUserOffset(int64_t offsetMs) {
    m_userOffsetMs = offsetMs;  // 写入设置菜单的全局 timing offset
}

void Clock::reset() {
    m_audioTimeMs.store(0, std::memory_order_release);  // 新曲加载，音频 cursor 归零
    m_anchorAudioMs = 0;                                // 插值锚点歌曲时间归零
    m_anchorTickMs = SDL_GetTicks64();                  // 以当前 tick 作为新锚点
    m_paused = false;                                   // 清除暂停标志
}

void Clock::pause() {
    m_paused = true;  // 冻结 interpolatedNowMs / songTimeAtTickMs
}

void Clock::resume() {
    // 恢复时用当前音频时间重新锚定，避免暂停期间 tick 累积
    m_anchorAudioMs = m_audioTimeMs.load(std::memory_order_acquire);  // 以当前 cursor 为锚点
    m_anchorTickMs = SDL_GetTicks64();                                // 重置锚点 tick，丢弃暂停期间 elapsed
    m_paused = false;                                                   // 恢复插值
}

} // namespace melody_matrix::core

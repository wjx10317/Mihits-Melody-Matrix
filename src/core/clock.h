/**
 * @file clock.h
 * @brief 权威游戏时钟（音频驱动）
 *
 * 文件职责：
 *   定义以音频播放位置为主、HostClock 外推为辅的歌曲时间时钟。
 *
 * 主要依赖：
 *   标准库 <atomic>、<cstdint>；实现依赖 time::HostClock（clock.cpp）。
 *
 * 在项目中的用法：
 *   Kernel 持有 Clock 实例；PlayingState 通过 AudioEngine 同步音频位置；
 *   判定与渲染均使用 interpolatedNowMs()，按键用 songTimeAtHostMs()。
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace melody_matrix::core {

/**
 * @brief 权威游戏时钟
 *
 * 绝对不要使用帧时间累积做判定！歌曲时间由音频 cursor 驱动，
 * 两次音频更新之间用 HostClock 外推。按键时刻必须来自 HostClock 同域。
 */
class Clock {
public:
    /**
     * @brief 从音频引擎同步当前播放位置
     * @param audioFrameTimeMs 音频当前歌曲时间（毫秒）
     *
     * 仅在 cursor 前进时刷新插值锚点。
     */
    void syncFromAudio(int64_t audioFrameTimeMs);

    /// 当前歌曲时间（含用户校准偏移，不含 HostClock 插值）
    int64_t nowMs() const;

    /// 插值后的歌曲时间（cursor 两次更新之间用 HostClock 外推）
    int64_t interpolatedNowMs() const;

    /**
     * @brief 将 HostClock 毫秒换算为歌曲时间
     * @param hostMs HostClock::nowMs() 同域的单调毫秒
     */
    int64_t songTimeAtHostMs(int64_t hostMs) const;

    /// 设置用户全局偏移（毫秒，可正可负）
    void setUserOffset(int64_t offsetMs);

    void reset();
    void pause();
    void resume();

private:
    int64_t elapsedMsSinceAnchor() const;

    std::atomic<int64_t> m_audioTimeMs{0};  ///< 最近一次音频 cursor
    int64_t m_userOffsetMs = 0;             ///< 用户校准偏移
    int64_t m_anchorAudioMs = 0;            ///< 插值锚点：音频时间
    int64_t m_anchorHostMs = 0;             ///< 插值锚点：HostClock ms
    bool m_paused = false;
};

} // namespace melody_matrix::core

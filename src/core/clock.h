#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace melody_matrix::core {

/// 权威游戏时钟：由音频播放位置驱动。
/// 绝对不要使用帧时间累积进行判定！
class Clock {
public:
    /// 由 AudioEngine 在每帧开始时调用，用于同步音频位置。
    void syncFromAudio(int64_t audioFrameTimeMs);

    /// 获取当前歌曲时间（应用用户校准偏移量），单位毫秒。
    int64_t nowMs() const;

    /// 获取插值时间：在音频同步之间使用高分辨率计时器
    /// 以实现平滑渲染（音频回调粒度约为 10ms）。
    int64_t interpolatedNowMs() const;

    /// 设置用户校准偏移量（来自设置，可以为正或负）
    void setUserOffset(int64_t offsetMs);

    /// 重置时钟（新歌曲加载时）
    void reset();

    /// 暂停/恢复时间插值
    void pause();
    void resume();

private:
    std::atomic<int64_t> m_audioTimeMs{0};                     // Latest audio time
    int64_t m_userOffsetMs = 0;                                  // User calibration
    std::chrono::steady_clock::time_point m_anchor;             // High-res anchor
    int64_t m_anchorAudioMs = 0;                                // Audio time at anchor
    bool m_paused = false;
};

} // namespace melody_matrix::core

#pragma once

#include <atomic>
#include <cstdint>

namespace melody_matrix::core {

/// 权威游戏时钟：由音频播放位置驱动。
/// 绝对不要使用帧时间累积进行判定！
/// 歌曲时间插值与 SDL 事件 timestamp 均基于 SDL_GetTicks64，避免双时钟漂移。
class Clock {
public:
    /// 由 AudioEngine 在每帧开始时调用，用于同步音频位置。
    void syncFromAudio(int64_t audioFrameTimeMs);

    /// 获取当前歌曲时间（应用用户校准偏移量），单位毫秒。
    int64_t nowMs() const;

    /// 获取插值时间：在音频 cursor 两次更新之间用 SDL tick 外推。
    int64_t interpolatedNowMs() const;

    /// 将 SDL 事件 timestamp（毫秒）换算到歌曲时间。
    int64_t songTimeAtTickMs(uint64_t tickMs) const;

    /// 设置用户校准偏移量（来自设置，可以为正或负）
    void setUserOffset(int64_t offsetMs);

    /// 重置时钟（新歌曲加载时）
    void reset();

    /// 暂停/恢复时间插值
    void pause();
    void resume();

private:
    int64_t elapsedMsSinceAnchor() const;

    std::atomic<int64_t> m_audioTimeMs{0};
    int64_t m_userOffsetMs = 0;
    int64_t m_anchorAudioMs = 0;
    uint64_t m_anchorTickMs = 0;
    bool m_paused = false;
};

} // namespace melody_matrix::core

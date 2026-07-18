/**
 * @file audio_playhead.h
 * @brief 听感/播放位置抽象（AudioPlayhead）
 *
 * 表示「音频播到哪了」，与 HostClock（主机时刻）分离。
 * C1：PlayingState 经 MiniaudioCursorPlayhead 同步 Clock（仍为 write cursor）。
 * C2：WasapiPlayhead（IAudioClock）替换实现，上层接口不变。
 */
#pragma once

#include <cstdint>

namespace melody_matrix::time {

/**
 * @brief 播放位置查询接口（设备或缓冲游标）
 *
 * 实现不得泄漏进 JudgeQueue；由音频层适配后，再经 core::Clock 同步。
 */
class IAudioPlayhead {
public:
    virtual ~IAudioPlayhead() = default;

    /// PCM 帧位置（相对当前播放流起点；语义由实现定义）
    virtual int64_t positionFrames() const = 0;

    /// 采样率（Hz）；无效时实现可返回 0
    virtual int32_t sampleRate() const = 0;

    /// 由 frames / sampleRate 导出的毫秒位置
    virtual int64_t positionMs() const = 0;

    /// 歌曲时间线 epoch 重置（play / seek 后调用）；默认无操作
    virtual void noteTimelineEpoch(int64_t /*songMs*/) {}

    /// 调试名：如 "wasapi" / "miniaudio-cursor"
    virtual const char* backendName() const { return "unknown"; }
};

} // namespace melody_matrix::time

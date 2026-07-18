/**
 * @file miniaudio_cursor_playhead.h
 * @brief IAudioPlayhead 的 miniaudio write-cursor 适配
 *
 * 语义仍是 data-source 已读取帧，不是 DAC 听感位置。
 * C2 将由 WasapiPlayhead 替换；上层只依赖 IAudioPlayhead。
 */
#pragma once

#include "time/audio_playhead.h"

#include <memory>

namespace melody_matrix::audio {

class AudioEngine;

/// 基于 AudioEngine::queryPlaybackCursor 的 Playhead
class MiniaudioCursorPlayhead final : public time::IAudioPlayhead {
public:
    explicit MiniaudioCursorPlayhead(const AudioEngine& engine);

    int64_t positionFrames() const override;
    int32_t sampleRate() const override;
    int64_t positionMs() const override;

    const char* backendName() const override { return "miniaudio-cursor"; }

private:
    const AudioEngine* m_engine = nullptr;
};

std::unique_ptr<time::IAudioPlayhead> makeMiniaudioCursorPlayhead(const AudioEngine& engine);

} // namespace melody_matrix::audio

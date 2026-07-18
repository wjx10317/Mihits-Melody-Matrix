/**
 * @file miniaudio_cursor_playhead.cpp
 * @brief MiniaudioCursorPlayhead 实现
 */
#include "audio/miniaudio_cursor_playhead.h"

#include "audio/audio_engine.h"

namespace melody_matrix::audio {

MiniaudioCursorPlayhead::MiniaudioCursorPlayhead(const AudioEngine& engine)
    : m_engine(&engine) {}

int64_t MiniaudioCursorPlayhead::positionFrames() const {
    if (!m_engine) return 0;
    int64_t frames = 0;
    int32_t sampleRate = 0;
    if (!m_engine->queryPlaybackCursor(frames, sampleRate)) return 0;
    return frames;
}

int32_t MiniaudioCursorPlayhead::sampleRate() const {
    if (!m_engine) return 0;
    int64_t frames = 0;
    int32_t sampleRate = 0;
    if (!m_engine->queryPlaybackCursor(frames, sampleRate)) return 0;
    return sampleRate;
}

int64_t MiniaudioCursorPlayhead::positionMs() const {
    if (!m_engine) return 0;
    int64_t frames = 0;
    int32_t sampleRate = 0;
    if (!m_engine->queryPlaybackCursor(frames, sampleRate) || sampleRate <= 0) return 0;
    return (frames * 1000 + sampleRate / 2) / sampleRate;
}

std::unique_ptr<time::IAudioPlayhead> makeMiniaudioCursorPlayhead(const AudioEngine& engine) {
    return std::make_unique<MiniaudioCursorPlayhead>(engine);
}

} // namespace melody_matrix::audio

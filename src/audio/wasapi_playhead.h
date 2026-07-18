/**
 * @file wasapi_playhead.h
 * @brief 尝试用 WASAPI IAudioClock 构造听感 Playhead；失败返回 nullptr
 */
#pragma once

#include "time/audio_playhead.h"

#include <memory>

namespace melody_matrix::audio {

class AudioEngine;

/// Windows：从 miniaudio 引擎的 WASAPI IAudioClient 取 IAudioClock。
/// 非 WASAPI / 失败时返回 nullptr，由调用方回退 MiniaudioCursorPlayhead。
std::unique_ptr<time::IAudioPlayhead> tryMakeWasapiPlayhead(AudioEngine& engine);

} // namespace melody_matrix::audio

/**
 * @file wasapi_playhead_stub.cpp
 * @brief 非 Windows：tryMakeWasapiPlayhead 恒返回 nullptr
 */
#ifndef _WIN32

#include "audio/wasapi_playhead.h"

namespace melody_matrix::audio {

std::unique_ptr<time::IAudioPlayhead> tryMakeWasapiPlayhead(AudioEngine& /*engine*/) {
    return nullptr;
}

} // namespace melody_matrix::audio

#endif

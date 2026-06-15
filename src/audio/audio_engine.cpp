#include "audio_engine.h"
#include "util/logger.h"

// 必须在 cpp 中定义 MINIAUDIO_IMPLEMENTATION，且只能定义一次
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <cmath>
#include <algorithm>

namespace melody_matrix::audio {

// ============================================================
// 工具函数：绕过 Windows min/max 宏
// ============================================================
static float mm_min(float a, float b) { return a < b ? a : b; }
static float mm_max(float a, float b) { return a > b ? a : b; }
static float mm_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static int64_t mm_clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ============================================================
// seek 辅助函数 —— 秒 → PCM帧（miniaudio 无 set_cursor_in_seconds）
// ============================================================
static void seekSoundToSeconds(ma_sound* sound, float seconds) {
    if (!sound) return;
    const ma_engine* engine = ma_sound_get_engine(sound);
    if (!engine) return;
    ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
    ma_uint64 frameIdx = static_cast<ma_uint64>(seconds * static_cast<float>(sampleRate));
    ma_sound_seek_to_pcm_frame(sound, frameIdx);
}

// ============================================================
// ActiveSound::release —— 释放 ma_sound（需 miniaudio.h，故放 .cpp）
// ============================================================
void ActiveSound::release() {
    if (sound) {
        ma_sound_stop(sound);
        ma_sound_uninit(sound);
        ma_free(sound, nullptr);
        sound = nullptr;
    }
}

// ============================================================
// init / shutdown
// ============================================================
bool AudioEngine::init() {
    if (m_initialized) return true;

    MM_LOG_INFO("Audio", "Initializing audio engine...");

    m_engine = (ma_engine*)ma_malloc(sizeof(ma_engine), nullptr);
    if (!m_engine) return false;
    std::memset(m_engine, 0, sizeof(ma_engine));

    ma_result result = ma_engine_init(nullptr, m_engine);
    if (result != MA_SUCCESS) {
        MM_LOG_ERROR("Audio", "Failed to init miniaudio engine: " + std::to_string(result));
        ma_free(m_engine, nullptr);
        m_engine = nullptr;
        return false;
    }

    m_initialized = true;
    MM_LOG_INFO("Audio", "Audio engine initialized successfully");
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    m_activeSounds.clear();  // 析构函数会自动释放 ma_sound

    if (m_engine) {
        ma_engine_uninit(m_engine);
        ma_free(m_engine, nullptr);
        m_engine = nullptr;
    }

    m_initialized = false;
    MM_LOG_INFO("Audio", "Audio engine shut down");
}

// ============================================================
// createSound / destroySound
// ============================================================
ma_sound* AudioEngine::createSound(const std::string& filePath, bool streaming) {
    if (!m_initialized || !m_engine) return nullptr;

    auto* sound = (ma_sound*)ma_malloc(sizeof(ma_sound), nullptr);
    if (!sound) return nullptr;
    // 不 memset —— ma_sound_init_from_file 会正确初始化

    ma_uint32 flags = streaming ? MA_SOUND_FLAG_STREAM : 0;

    ma_result result = ma_sound_init_from_file(
        m_engine, filePath.c_str(), flags, nullptr, nullptr, sound);
    if (result != MA_SUCCESS) {
        MM_LOG_WARN("Audio", "createSound failed: " + filePath + " err=" + std::to_string(result));
        ma_free(sound, nullptr);
        return nullptr;
    }

    return sound;
}

void AudioEngine::destroySound(ma_sound* sound) {
    if (!sound) return;
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    ma_free(sound, nullptr);
}

// ============================================================
// playSong（兼容原有接口，游戏内播放，单路）
// ============================================================
bool AudioEngine::playSong(const std::string& filePath) {
    if (!m_initialized) return false;

    // 停止当前所有声音
    stop();

    ma_sound* sound = createSound(filePath, /*streaming=*/true);
    if (!sound) return false;

    ma_sound_set_volume(sound, m_volume);
    ma_sound_start(sound);

    // 放入池子
    ActiveSound slot;
    slot.sound = sound;
    slot.type  = SoundType::BGM;
    m_activeSounds.push_back(std::move(slot));

    MM_LOG_INFO("Audio", "Playing (playSong): " + filePath);
    return true;
}

void AudioEngine::pause() {
    for (auto& snd : m_activeSounds) {
        if (snd.sound) ma_sound_stop(snd.sound);
    }
}

void AudioEngine::resume() {
    for (auto& snd : m_activeSounds) {
        if (snd.sound) ma_sound_start(snd.sound);
    }
}

void AudioEngine::stop() {
    m_activeSounds.clear();  // 析构自动释放
}

void AudioEngine::setVolume(float volume) {
    m_volume = mm_clamp(volume, 0.0f, 1.0f);
    // 更新所有活动声音的实际音量
    for (auto& snd : m_activeSounds) {
        if (snd.sound) {
            ma_sound_set_volume(snd.sound, calcVolume(snd.type));
        }
    }
}

// ============================================================
// playPreview —— 选歌界面预览播放
// ============================================================
void AudioEngine::playPreview(const std::string& filePath,
                              int64_t startTimeMs,
                              float   fadeInDurationS,
                              int64_t previewDurationMs) {
    if (!m_initialized) return;
    if (filePath.empty()) return;

    // ---- 如果当前有同一路径的预览在播，不重启 ----
    for (auto& snd : m_activeSounds) {
        if (snd.sound && snd.type == SoundType::Preview &&
            snd.filePath == filePath) {
            // 同一首歌，继续播放即可
            return;
        }
    }

    // ---- 停止旧的预览声音（淡出） ----
    if (fadeInDurationS > 0.0f) {
        // 标记所有 Preview 类型的声音为淡出
        for (auto& snd : m_activeSounds) {
            if (snd.sound && snd.type == SoundType::Preview && !snd.isFadingOut) {
                snd.isFadingOut  = true;
                snd.isFadingIn   = false;
                snd.fadeTimer    = 0.0f;
                snd.fadeDuration = fadeInDurationS;
                snd.fadeStartVol = ma_sound_get_volume(snd.sound);
                snd.fadeTargetVol = 0.0f;
            }
        }
    } else {
        // 无淡入，立即停止所有 Preview
        auto it = m_activeSounds.begin();
        while (it != m_activeSounds.end()) {
            if (it->type == SoundType::Preview) {
                it = m_activeSounds.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- 创建新声音 ----
    ma_sound* sound = createSound(filePath, /*streaming=*/true);
    if (!sound) {
        MM_LOG_WARN("Audio", "playPreview: failed to create sound: " + filePath);
        return;
    }

    // seek 到 previewTime（用 set_cursor_in_seconds，API 简单可靠）
    if (startTimeMs > 0) {
        float startS = static_cast<float>(startTimeMs) / 1000.0f;
        seekSoundToSeconds(sound, startS);
    }

    // 设置初始音量为 0（准备淡入）
    float targetVol = calcVolume(SoundType::Preview);
    if (fadeInDurationS > 0.0f) {
        ma_sound_set_volume(sound, 0.0f);
    } else {
        ma_sound_set_volume(sound, targetVol);
    }

    ma_sound_start(sound);

    // 放入池子
    ActiveSound slot;
    slot.sound          = sound;
    slot.type           = SoundType::Preview;
    slot.filePath       = filePath;
    slot.previewStartMs = startTimeMs;
    slot.previewEndMs   = (previewDurationMs > 0) ? (startTimeMs + previewDurationMs) : 0;
    slot.isFadingIn    = (fadeInDurationS > 0.0f);
    slot.isFadingOut    = false;
    slot.fadeTimer      = 0.0f;
    slot.fadeDuration   = fadeInDurationS;
    slot.fadeStartVol   = 0.0f;
    slot.fadeTargetVol  = targetVol;
    m_activeSounds.push_back(std::move(slot));

    // 限制最大并发数
    while (static_cast<int>(m_activeSounds.size()) > MAX_ACTIVE_SOUNDS) {
        // 移除最老的 Preview 声音（非淡出中的）
        auto it = m_activeSounds.begin();
        while (it != m_activeSounds.end()) {
            if (it->type == SoundType::Preview && !it->isFadingOut) {
                it = m_activeSounds.erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

    MM_LOG_INFO("Audio", "Preview: " + filePath + " startMs=" + std::to_string(startTimeMs));
}

// ============================================================
// stopWithFade
// ============================================================
void AudioEngine::stopWithFade(float durationS) {
    if (durationS > 0.0f) {
        for (auto& snd : m_activeSounds) {
            if (!snd.sound || snd.isFadingOut) continue;
            snd.isFadingOut  = true;
            snd.isFadingIn   = false;
            snd.fadeTimer    = 0.0f;
            snd.fadeDuration = durationS;
            snd.fadeStartVol = ma_sound_get_volume(snd.sound);
            snd.fadeTargetVol = 0.0f;
        }
    } else {
        m_activeSounds.clear();
    }
}

// ============================================================
// update —— 每帧调用，驱动淡入淡出 + 预览循环
// ============================================================
void AudioEngine::update(float dt) {
    if (!m_initialized) return;

    for (size_t i = 0; i < m_activeSounds.size(); ) {
        auto& snd = m_activeSounds[i];

        if (!snd.sound) {
            // 声音已被释放，移除槽位
            snd = std::move(m_activeSounds.back());
            m_activeSounds.pop_back();
            continue;
        }

        // ---- 淡入处理 ----
        if (snd.isFadingIn && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);
            if (snd.fadeTimer >= snd.fadeDuration) {
                snd.isFadingIn = false;
            }
        }

        // ---- 淡出处理 ----
        if (snd.isFadingOut && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);
            if (snd.fadeTimer >= snd.fadeDuration) {
                // 淡出完成，释放
                ActiveSound tmp;
                tmp.sound = snd.sound;
                snd.sound = nullptr;
                tmp.release();
                // 移除槽位（swap-and-pop）
                snd = std::move(m_activeSounds.back());
                m_activeSounds.pop_back();
                continue;  // 不增加 i，当前位置已被新元素填充
            }
        }

        // ---- 预览循环检测 ----
        if (!snd.isFadingOut && snd.type == SoundType::Preview && snd.previewStartMs >= 0) {
            float cursorS = 0.0f;
            ma_sound_get_cursor_in_seconds(snd.sound, &cursorS);
            int64_t cursorMs = static_cast<int64_t>(cursorS * 1000.0);

            bool shouldLoop = false;
            if (snd.previewEndMs > 0 && cursorMs >= snd.previewEndMs) {
                shouldLoop = true;
            } else {
                // 播到结尾了
                float lengthS = 0.0f;
                ma_sound_get_length_in_seconds(snd.sound, &lengthS);
                if (lengthS > 0.0f && cursorMs >= static_cast<int64_t>(lengthS * 1000.0) - 500) {
                    shouldLoop = true;
                }
            }

            if (shouldLoop) {
                // seek 回 previewStartMs
                float startS = static_cast<float>(snd.previewStartMs) / 1000.0f;
                seekSoundToSeconds(snd.sound, startS);
            }
        }

        ++i;
    }
}

// ============================================================
// 查询接口
// ============================================================
int64_t AudioEngine::positionMs() const {
    for (const auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            float cursor = 0.0f;
            ma_sound_get_cursor_in_seconds(snd.sound, &cursor);
            return static_cast<int64_t>(cursor * 1000.0);
        }
    }
    return 0;
}

int64_t AudioEngine::durationMs() const {
    for (const auto& snd : m_activeSounds) {
        if (snd.sound) {
            float length = 0.0f;
            ma_sound_get_length_in_seconds(snd.sound, &length);
            return static_cast<int64_t>(length * 1000.0);
        }
    }
    return 0;
}

// ============================================================
// 音量分组控制
// ============================================================
void AudioEngine::setTypeVolume(SoundType type, float volume) {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx > 2) return;
    m_typeVolumes[idx] = mm_clamp(volume, 0.0f, 1.0f);
    // 更新该类型所有活动声音
    for (auto& snd : m_activeSounds) {
        if (snd.type == type && snd.sound) {
            ma_sound_set_volume(snd.sound, calcVolume(type));
        }
    }
}

float AudioEngine::getTypeVolume(SoundType type) const {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx > 2) return 1.0f;
    return m_typeVolumes[idx];
}

float AudioEngine::calcVolume(SoundType type) const {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx > 2) return m_volume;
    return m_volume * m_typeVolumes[idx];
}

} // namespace melody_matrix::audio

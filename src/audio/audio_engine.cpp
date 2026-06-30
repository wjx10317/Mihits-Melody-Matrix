#include "audio_engine.h"
#include "util/logger.h"

// MINIAUDIO_IMPLEMENTATION 仅能在单个 .cpp 中定义一次
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

// ──────────────────────────────────────────────────────
//  audio_engine.cpp — miniaudio 音频引擎实现
//
//  模块：引擎生命周期、ActiveSound 池、预览淡入淡出、
//  SFX 全量加载与轮转播放、res/ 路径探测。
// ──────────────────────────────────────────────────────

namespace melody_matrix::audio {

namespace {

/// 后缀名不区分大小写比较
static bool endsWithIgnoreCase(std::string_view name, std::string_view suffix) {    if (name.size() < suffix.size()) {
        return false;
    }
    const auto tail = name.substr(name.size() - suffix.size());
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(tail[i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

/// 相对 res 目录搜索顺序（兼容不同工作目录）
static const char* kResDirs[] = { "res", "../res", "../../res" };

static const char* sfxTypeName(SfxType type) {
    switch (type) {
    case SfxType::MenuClick:  return "MenuClick";
    case SfxType::MenuHit:    return "MenuHit";
    case SfxType::HitNormal:  return "HitNormal";
    case SfxType::SliderTick: return "SliderTick";
    default:                  return "Unknown";
    }
}

static const char* describeMaResult(ma_result result) {
    const char* desc = ma_result_description(result);
    return (desc && desc[0] != '\0') ? desc : "unknown error";
}

static std::string resolveResFile(const char* relativeFile) {
    for (const char* dir : kResDirs) {
        const std::filesystem::path path = std::filesystem::path(dir) / relativeFile;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }
    return {};
}

static void logSfxPathProbe(const char* sfxName, const char* relativeFile) {
    MM_LOG_WARN("Audio", "SFX not found: type=%s fixed=%s", sfxName, relativeFile);
    for (const char* dir : kResDirs) {
        const std::filesystem::path path = std::filesystem::path(dir) / relativeFile;
        const bool dirExists = std::filesystem::is_directory(dir);
        const bool fileExists = std::filesystem::exists(path);
        MM_LOG_WARN("Audio", "  probe dir=%s dirExists=%s file=%s fileExists=%s",
                    dir, dirExists ? "yes" : "no",
                    path.string().c_str(), fileExists ? "yes" : "no");
    }
}

static void logSfxLoadFailure(const char* sfxName, const std::string& filePath,
                              int slot, ma_result result) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(filePath, ec);
    std::string sizeInfo = "n/a";
    if (exists) {
        const auto size = std::filesystem::file_size(filePath, ec);
        if (!ec) {
            sizeInfo = std::to_string(size) + " bytes";
        }
    }

    MM_LOG_WARN("Audio",
                "SFX load failed: type=%s slot=%d path=%s exists=%s size=%s "
                "ma_result=%d (%s)",
                sfxName, slot, filePath.c_str(),
                exists ? "yes" : "no", sizeInfo.c_str(),
                static_cast<int>(result), describeMaResult(result));
}

static void logHitNormalDiscoveryMiss() {
    MM_LOG_WARN("Audio",
                "HitNormal: no file matching *_normal.{wav,ogg} or *-hitnormal.{wav,ogg}");
    for (const char* dir : kResDirs) {
        const std::filesystem::path dirPath(dir);
        if (!std::filesystem::is_directory(dirPath)) {
            MM_LOG_WARN("Audio", "  scan dir=%s (missing)", dir);
            continue;
        }

        MM_LOG_WARN("Audio", "  scan dir=%s", dir);
        bool listedAny = false;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".wav" && ext != ".ogg" && ext != ".WAV" && ext != ".OGG") {
                continue;
            }
            listedAny = true;
            MM_LOG_WARN("Audio", "    audio: %s", entry.path().filename().string().c_str());
        }
        if (!listedAny) {
            MM_LOG_WARN("Audio", "    (no .wav/.ogg files)");
        }
    }
}

/// 击打音后缀：测试用 *_normal.*，osu 式 *-hitnormal.*
static constexpr const char* kHitNormalSuffixes[] = {
    "_normal.wav", "_normal.ogg",
    "-hitnormal.wav", "-hitnormal.ogg",
};

/// 在 res 目录中按文件名后缀匹配音效，多个命中时取最近修改的文件。
static std::string findSfxBySuffixes(std::initializer_list<const char*> suffixes) {
    std::string bestPath;
    std::filesystem::file_time_type bestTime{};
    bool found = false;

    for (const char* dir : kResDirs) {
        const std::filesystem::path dirPath(dir);
        if (!std::filesystem::is_directory(dirPath)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            bool matched = false;
            for (const char* suffix : suffixes) {
                if (endsWithIgnoreCase(filename, suffix)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }

            const auto mtime = entry.last_write_time();
            if (!found || mtime > bestTime) {
                found = true;
                bestTime = mtime;
                bestPath = entry.path().string();
            }
        }
    }

    return bestPath;
}

static ma_result loadSfxInstance(ma_engine* engine, const std::string& filePath,
                                 ma_sound** outSound) {
    if (outSound) {
        *outSound = nullptr;
    }
    if (!engine || filePath.empty()) {
        return MA_INVALID_ARGS;
    }

    auto* sound = (ma_sound*)ma_malloc(sizeof(ma_sound), nullptr);
    if (!sound) {
        return MA_OUT_OF_MEMORY;
    }

    const ma_result result = ma_sound_init_from_file(
        engine, filePath.c_str(), 0, nullptr, nullptr, sound);
    if (result != MA_SUCCESS) {
        ma_free(sound, nullptr);
        return result;
    }

    if (outSound) {
        *outSound = sound;
    } else {
        ma_sound_uninit(sound);
        ma_free(sound, nullptr);
    }
    return MA_SUCCESS;
}

static int loadSfxPool(ma_engine* engine,
                       ma_sound* pool[],
                       int poolSize,
                       const std::string& filePath,
                       const char* sfxName) {
    int loadedCount = 0;
    for (int slot = 0; slot < poolSize; ++slot) {
        ma_sound* sound = nullptr;
        const ma_result result = loadSfxInstance(engine, filePath, &sound);
        if (result != MA_SUCCESS) {
            logSfxLoadFailure(sfxName, filePath, slot, result);
            break;
        }
        pool[slot] = sound;
        ++loadedCount;
    }

    if (loadedCount > 0 && loadedCount < poolSize) {
        MM_LOG_WARN("Audio",
                    "SFX partial load: type=%s path=%s loaded=%d/%d",
                    sfxName, filePath.c_str(), loadedCount, poolSize);
    }
    return loadedCount;
}

} // namespace

// ── 工具：绕过 Windows min/max 宏 ──

static float mm_min(float a, float b) { return a < b ? a : b; }
static float mm_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// seek 辅助：秒 → PCM 帧（miniaudio 无 set_cursor_in_seconds）
static void seekSoundToSeconds(ma_sound* sound, float seconds) {
    if (!sound) return;
    const ma_engine* engine = ma_sound_get_engine(sound);
    if (!engine) return;
    ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
    ma_uint64 frameIdx = static_cast<ma_uint64>(seconds * static_cast<float>(sampleRate));
    ma_sound_seek_to_pcm_frame(sound, frameIdx);
}

/// 释放 ActiveSound 持有的 ma_sound
void ActiveSound::release() {    if (sound) {
        ma_sound_stop(sound);
        ma_sound_uninit(sound);
        ma_free(sound, nullptr);
        sound = nullptr;
    }
}


// ── 引擎 init / shutdown ──

bool AudioEngine::init() {
    if (m_initialized) return true;  ///< 防止重复初始化

    MM_LOG_INFO("Audio", "Initializing audio engine...");

    m_engine = (ma_engine*)ma_malloc(sizeof(ma_engine), nullptr);
    if (!m_engine) return false;
    std::memset(m_engine, 0, sizeof(ma_engine));

    ma_result result = ma_engine_init(nullptr, m_engine);  ///< 默认设备与采样率
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

    // 释放音效资源（每个类型 SFX_POOL_SIZE 个实例）
    for (int i = 0; i < static_cast<int>(SfxType::Count); ++i) {
        for (int j = 0; j < SFX_POOL_SIZE; ++j) {
            if (m_sfxSounds[i][j]) {
                destroySound(m_sfxSounds[i][j]);
                m_sfxSounds[i][j] = nullptr;
            }
        }
        m_sfxRoundRobin[i] = 0;
    }
    m_sfxLoaded = false;

    if (m_engine) {
        ma_engine_uninit(m_engine);
        ma_free(m_engine, nullptr);
        m_engine = nullptr;
    }

    m_initialized = false;
    MM_LOG_INFO("Audio", "Audio engine shut down");
}

// ── createSound / destroySound ──

ma_sound* AudioEngine::createSound(const std::string& filePath, bool streaming) {
    if (!m_initialized || !m_engine) return nullptr;

    auto* sound = (ma_sound*)ma_malloc(sizeof(ma_sound), nullptr);
    if (!sound) return nullptr;
    // ma_sound_init_from_file 会完整初始化，无需 memset
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

// ── playSong：游戏内单路 BGM ──

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
// ── playPreview：选歌预览（同路径不重启、旧预览淡出）──

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

// ── stopWithFade ──

void AudioEngine::stopWithFade(float durationS) {    if (durationS > 0.0f) {
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

// ── update：淡入淡出 + 预览循环（swap-and-pop 移除槽位）──

void AudioEngine::update(float dt) {    if (!m_initialized) return;

    for (size_t i = 0; i < m_activeSounds.size(); ) {
        auto& snd = m_activeSounds[i];

        if (!snd.sound) {
            snd = std::move(m_activeSounds.back());  ///< O(1) 移除空槽
            m_activeSounds.pop_back();
            continue;
        }

        if (snd.isFadingIn && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);
            if (snd.fadeTimer >= snd.fadeDuration) {
                snd.isFadingIn = false;
            }
        }

        // 淡出：完成后 release 并 swap-and-pop
        if (snd.isFadingOut && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);
            if (snd.fadeTimer >= snd.fadeDuration) {
                ActiveSound tmp;
                tmp.sound = snd.sound;
                snd.sound = nullptr;
                tmp.release();
                snd = std::move(m_activeSounds.back());
                m_activeSounds.pop_back();
                continue;
            }
        }

        // 预览：到达 previewEnd 或文件末尾前 500ms 则 seek 回起点
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

// ── 查询：播放状态、位置、时长、seek ──

bool AudioEngine::isPlaying() const {    for (const auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            if (ma_sound_is_playing(snd.sound)) {
                return true;
            }
        }
    }
    return false;
}

int64_t AudioEngine::positionMs() const {
    for (const auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            // PCM 帧整数运算 + 四舍五入，避免 float 秒数截断误差
            ma_uint64 cursorFrames = 0;
            ma_sound_get_cursor_in_pcm_frames(snd.sound, &cursorFrames);
            ma_uint32 sampleRate = ma_engine_get_sample_rate(m_engine);
            if (sampleRate == 0) sampleRate = 48000;
            return static_cast<int64_t>((cursorFrames * 1000 + sampleRate / 2) / sampleRate);
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

void AudioEngine::seekTo(int64_t positionMs) {
    for (auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            ma_uint64 frameIndex = static_cast<ma_uint64>(
                positionMs * ma_engine_get_sample_rate(m_engine) / 1000);
            ma_sound_seek_to_pcm_frame(snd.sound, frameIndex);
            return;
        }
    }
}

// ── 分组音量 ──

void AudioEngine::setTypeVolume(SoundType type, float volume) {    int idx = static_cast<int>(type);
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

// ── SFX：全量加载 + 轮转播放 ──

bool AudioEngine::loadSfx() {
    if (!m_initialized) return false;
    if (m_sfxLoaded) return true;

    struct SfxSpec {
        SfxType type;
        const char* fixedFile; ///< 固定文件名（相对 res/）
        bool discoverHitNormal; ///< 扫描 res/ 下击打音，无固定回退
    };

    static const SfxSpec kSfxSpecs[] = {
        { SfxType::MenuClick,  "menuclick.wav",         false },
        { SfxType::MenuHit,    "menuhit.wav",           false },
        { SfxType::HitNormal,  nullptr,                 true },
        { SfxType::SliderTick, "normal-slidertick.wav", false },
    };

    for (const SfxSpec& spec : kSfxSpecs) {
        const char* sfxName = sfxTypeName(spec.type);
        std::string filePath;
        if (spec.discoverHitNormal) {
            filePath = findSfxBySuffixes({
                kHitNormalSuffixes[0], kHitNormalSuffixes[1],
                kHitNormalSuffixes[2], kHitNormalSuffixes[3],
            });
            if (filePath.empty()) {
                logHitNormalDiscoveryMiss();
                continue;
            }
        } else if (spec.fixedFile) {
            filePath = resolveResFile(spec.fixedFile);
            if (filePath.empty()) {
                logSfxPathProbe(sfxName, spec.fixedFile);
                continue;
            }
        } else {
            MM_LOG_WARN("Audio", "SFX spec missing path: type=%s", sfxName);
            continue;
        }

        const int idx = static_cast<int>(spec.type);
        const int loadedCount = loadSfxPool(
            m_engine, m_sfxSounds[idx], SFX_POOL_SIZE, filePath, sfxName);
        if (loadedCount == 0) {
            continue;
        }

        MM_LOG_INFO("Audio", "SFX loaded: type=%s slots=%d path=%s",
                    sfxName, loadedCount, filePath.c_str());
    }

    m_sfxLoaded = true;
    return true;
}

void AudioEngine::playSfx(SfxType type) {
    if (!m_initialized || !m_sfxLoaded) return;
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(SfxType::Count)) return;

    // 轮转 slot，快速连击不截断上一实例
    int slot = m_sfxRoundRobin[idx];
    m_sfxRoundRobin[idx] = (slot + 1) % SFX_POOL_SIZE;

    ma_sound* sound = m_sfxSounds[idx][slot];
    if (!sound) {
        // 当前 slot 未加载，尝试其他 slot
        for (int s = 0; s < SFX_POOL_SIZE; ++s) {
            if (m_sfxSounds[idx][s]) {
                sound = m_sfxSounds[idx][s];
                break;
            }
        }
        if (!sound) return;
    }

    // seek 到开头，然后播放
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_set_volume(sound, calcVolume(SoundType::Effect));
    ma_sound_start(sound);
}

} // namespace melody_matrix::audio

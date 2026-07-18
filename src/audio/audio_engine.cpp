#include "audio_engine.h"  // AudioEngine 类与 ActiveSound 结构体
#include "util/logger.h"    // MM_LOG_INFO / MM_LOG_WARN / MM_LOG_ERROR

// MINIAUDIO_IMPLEMENTATION 仅能在单个 .cpp 中定义一次
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>  // miniaudio 引擎与 ma_sound API

#include <cmath>        // std::pow 等（本文件间接使用）
#include <algorithm>    // （预留）容器算法
#include <cctype>       // std::tolower：后缀名大小写不敏感比较
#include <filesystem>   // 路径拼接与文件存在性检查
#include <string_view>  // endsWithIgnoreCase 参数

// ──────────────────────────────────────────────────────
//  audio_engine.cpp — miniaudio 音频引擎实现
//
//  模块：引擎生命周期、ActiveSound 池、预览淡入淡出、
//  SFX 全量加载与轮转播放、res/ 路径探测。
// ──────────────────────────────────────────────────────

namespace melody_matrix::audio {

namespace {

/// 后缀名不区分大小写比较
static bool endsWithIgnoreCase(std::string_view name, std::string_view suffix) {
    if (name.size() < suffix.size()) {
        return false;  // 文件名比后缀短，不可能匹配
    }
    const auto tail = name.substr(name.size() - suffix.size());  // 取文件名尾部与 suffix 等长片段
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(tail[i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;  // 逐字符忽略大小写比较
        }
    }
    return true;  // 后缀完全匹配
}

/// 相对 res 目录搜索顺序（兼容不同工作目录）
static const char* kResDirs[] = { "res", "../res", "../../res" };

// SfxType 枚举转可读字符串，供日志输出
static const char* sfxTypeName(SfxType type) {
    switch (type) {
    case SfxType::MenuClick:  return "MenuClick";
    case SfxType::MenuHit:    return "MenuHit";
    case SfxType::HitNormal:  return "HitNormal";
    case SfxType::SliderTick: return "SliderTick";
    default:                  return "Unknown";
    }
}

// miniaudio 错误码转人类可读描述
static const char* describeMaResult(ma_result result) {
    const char* desc = ma_result_description(result);
    return (desc && desc[0] != '\0') ? desc : "unknown error";
}

// 在 kResDirs 中查找 relativeFile，返回首个存在的绝对路径字符串
static std::string resolveResFile(const char* relativeFile) {
    for (const char* dir : kResDirs) {
        const std::filesystem::path path = std::filesystem::path(dir) / relativeFile;
        if (std::filesystem::exists(path)) {
            return path.string();  // 找到即返回
        }
    }
    return {};  // 所有候选目录均未找到
}

// 音效文件未找到时，逐目录探测并输出诊断日志
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

// 单个 ma_sound 加载失败时的详细诊断（路径、存在性、文件大小、错误码）
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

// HitNormal 扫描失败时，列出各 res 目录下的音频文件供排查
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
                continue;  // 跳过目录等非普通文件
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".wav" && ext != ".ogg" && ext != ".WAV" && ext != ".OGG") {
                continue;  // 只列出音频扩展名
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
    std::string bestPath;                    // 当前最佳匹配路径
    std::filesystem::file_time_type bestTime{};  // 最佳匹配的修改时间
    bool found = false;                      // 是否已找到至少一个匹配

    for (const char* dir : kResDirs) {
        const std::filesystem::path dirPath(dir);
        if (!std::filesystem::is_directory(dirPath)) {
            continue;  // 目录不存在则跳过
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
                continue;  // 文件名不匹配任何后缀
            }

            const auto mtime = entry.last_write_time();
            if (!found || mtime > bestTime) {
                found = true;
                bestTime = mtime;           // 取最新修改的文件
                bestPath = entry.path().string();
            }
        }
    }

    return bestPath;  // 空字符串表示未找到
}

// 从文件加载单个 ma_sound 实例
static ma_result loadSfxInstance(ma_engine* engine, const std::string& filePath,
                                 ma_sound** outSound) {
    if (outSound) {
        *outSound = nullptr;  // 输出指针先置空
    }
    if (!engine || filePath.empty()) {
        return MA_INVALID_ARGS;  // 参数无效
    }

    auto* sound = (ma_sound*)ma_malloc(sizeof(ma_sound), nullptr);
    if (!sound) {
        return MA_OUT_OF_MEMORY;
    }

    // 非流式加载：音效文件较小，完整解码到内存
    const ma_result result = ma_sound_init_from_file(
        engine, filePath.c_str(), 0, nullptr, nullptr, sound);
    if (result != MA_SUCCESS) {
        ma_free(sound, nullptr);  // 初始化失败则释放内存
        return result;
    }

    if (outSound) {
        *outSound = sound;  // 成功：转移所有权给调用方
    } else {
        ma_sound_uninit(sound);  // 探测性加载：立即释放
        ma_free(sound, nullptr);
    }
    return MA_SUCCESS;
}

// 为同一音效文件加载 poolSize 个实例（轮转播放池）
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
            break;  // 某一 slot 失败则停止继续加载
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
    return v < lo ? lo : (v > hi ? hi : v);  // 将 v 限制在 [lo, hi]
}

/// seek 辅助：秒 → PCM 帧（miniaudio 无 set_cursor_in_seconds）
static void seekSoundToSeconds(ma_sound* sound, float seconds) {
    if (!sound) return;
    const ma_engine* engine = ma_sound_get_engine(sound);
    if (!engine) return;
    ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);  // 引擎采样率
    ma_uint64 frameIdx = static_cast<ma_uint64>(seconds * static_cast<float>(sampleRate));
    ma_sound_seek_to_pcm_frame(sound, frameIdx);  // 按 PCM 帧索引 seek
}

/// 释放 ActiveSound 持有的 ma_sound
void ActiveSound::release() {
    if (sound) {
        ma_sound_stop(sound);      // 停止播放
        ma_sound_uninit(sound);    // 反初始化 miniaudio 对象
        ma_free(sound, nullptr);   // 释放堆内存
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
        m_sfxRoundRobin[i] = 0;  // 轮转索引归零
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
    ma_uint32 flags = streaming ? MA_SOUND_FLAG_STREAM : 0;  // 流式适合长 BGM/预览

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
    if (!m_initialized) return false;  // 引擎未初始化则无法播放

    // 停止当前所有声音（清空 ActiveSound 池，析构释放旧 BGM/预览）
    stop();

    // 以流式方式创建 ma_sound，适合长音频且 seek 安全
    ma_sound* sound = createSound(filePath, /*streaming=*/true);
    if (!sound) return false;  // 文件加载失败

    ma_sound_set_volume(sound, m_volume);  // 应用当前全局音量
    ma_sound_start(sound);               // 开始播放

    // 将新 BGM 放入活动声音池
    ActiveSound slot;
    slot.sound = sound;           // 转移 ma_sound 所有权给 ActiveSound
    slot.type  = SoundType::BGM;  // 标记为游戏 BGM 类型
    m_activeSounds.push_back(std::move(slot));

    MM_LOG_INFO("Audio", "Playing (playSong): " + filePath);
    return true;
}

void AudioEngine::pause() {
    for (auto& snd : m_activeSounds) {
        if (snd.sound) ma_sound_stop(snd.sound);  // 暂停 = 停止解码（resume 可 restart）
    }
}

void AudioEngine::resume() {
    for (auto& snd : m_activeSounds) {
        if (snd.sound) ma_sound_start(snd.sound);  // 从当前 cursor 继续
    }
}

void AudioEngine::stop() {
    m_activeSounds.clear();  // 析构自动 release 所有 ma_sound
}

void AudioEngine::setVolume(float volume) {
    m_volume = mm_clamp(volume, 0.0f, 1.0f);  // 全局音量 clamp 到 [0,1]
    // 更新所有活动声音的实际音量
    for (auto& snd : m_activeSounds) {
        if (snd.sound) {
            ma_sound_set_volume(snd.sound, calcVolume(snd.type));  // 全局 × 分组
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

// ── update：淡入淡出 + 预览循环（swap-and-pop 移除槽位）──

void AudioEngine::update(float dt) {
    if (!m_initialized) return;  // 未初始化则跳过

    // 手动索引循环：移除元素时用 swap-and-pop，不递增 i
    for (size_t i = 0; i < m_activeSounds.size(); ) {
        auto& snd = m_activeSounds[i];  // 当前活动声音槽

        if (!snd.sound) {
            snd = std::move(m_activeSounds.back());  ///< O(1) 移除空槽
            m_activeSounds.pop_back();
            continue;  // 不递增 i，继续检查 swap 过来的元素
        }

        // ── 淡入处理 ──
        if (snd.isFadingIn && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;  // 累计淡入时间
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);  // 线性进度 [0,1]
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);  // 线性插值音量
            if (snd.fadeTimer >= snd.fadeDuration) {
                snd.isFadingIn = false;  // 淡入完成
            }
        }

        // 淡出：完成后 release 并 swap-and-pop
        if (snd.isFadingOut && snd.fadeDuration > 0.0f) {
            snd.fadeTimer += dt;
            float t = mm_min(snd.fadeTimer / snd.fadeDuration, 1.0f);
            float vol = snd.fadeStartVol + (snd.fadeTargetVol - snd.fadeStartVol) * t;
            ma_sound_set_volume(snd.sound, vol);
            if (snd.fadeTimer >= snd.fadeDuration) {
                // 淡出结束：手动 release 后 swap-and-pop 移除槽位
                ActiveSound tmp;
                tmp.sound = snd.sound;  // 暂存 sound 指针
                snd.sound = nullptr;    // 避免析构重复 release
                tmp.release();          // 停止并释放 ma_sound
                snd = std::move(m_activeSounds.back());
                m_activeSounds.pop_back();
                continue;  // 不递增 i
            }
        }

        // 预览：到达 previewEnd 或文件末尾前 500ms 则 seek 回起点
        if (!snd.isFadingOut && snd.type == SoundType::Preview && snd.previewStartMs >= 0) {
            float cursorS = 0.0f;
            ma_sound_get_cursor_in_seconds(snd.sound, &cursorS);  // 当前播放位置（秒）
            int64_t cursorMs = static_cast<int64_t>(cursorS * 1000.0);

            bool shouldLoop = false;
            if (snd.previewEndMs > 0 && cursorMs >= snd.previewEndMs) {
                shouldLoop = true;  // 到达预览结束点
            } else {
                // 播到结尾了
                float lengthS = 0.0f;
                ma_sound_get_length_in_seconds(snd.sound, &lengthS);
                // 距文件末尾 500ms 内也 loop（previewEndMs=0 时的回退策略）
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

        ++i;  // 正常情况：处理下一槽
    }
}

// ── 查询：播放状态、位置、时长、seek ──

bool AudioEngine::isPlaying() const {
    for (const auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            if (ma_sound_is_playing(snd.sound)) {
                return true;  // 任一非淡出声音在播即返回 true
            }
        }
    }
    return false;
}

bool AudioEngine::queryPlaybackCursor(int64_t& outFrames, int32_t& outSampleRate) const {
    outFrames = 0;
    outSampleRate = 0;
    for (const auto& snd : m_activeSounds) {
        if (snd.sound && !snd.isFadingOut) {
            ma_uint64 cursorFrames = 0;
            ma_sound_get_cursor_in_pcm_frames(snd.sound, &cursorFrames);
            ma_uint32 sampleRate = m_engine ? ma_engine_get_sample_rate(m_engine) : 0;
            if (sampleRate == 0) sampleRate = 48000;
            outFrames = static_cast<int64_t>(cursorFrames);
            outSampleRate = static_cast<int32_t>(sampleRate);
            return true;
        }
    }
    return false;
}

int64_t AudioEngine::positionMs() const {
    int64_t frames = 0;
    int32_t sampleRate = 0;
    if (!queryPlaybackCursor(frames, sampleRate) || sampleRate <= 0) {
        return 0;
    }
    return (frames * 1000 + sampleRate / 2) / sampleRate;
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
            return;  // 只 seek 第一个有效声音
        }
    }
}

// ── 分组音量 ──

void AudioEngine::setTypeVolume(SoundType type, float volume) {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx > 2) return;  // 越界保护
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
    return m_volume * m_typeVolumes[idx];  // 实际音量 = 全局 × 分组
}

// ── SFX：全量加载 + 轮转播放 ──

bool AudioEngine::loadSfx() {
    if (!m_initialized) return false;
    if (m_sfxLoaded) return true;  // 已加载则幂等返回

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
            // HitNormal：按后缀扫描 res 目录
            filePath = findSfxBySuffixes({
                kHitNormalSuffixes[0], kHitNormalSuffixes[1],
                kHitNormalSuffixes[2], kHitNormalSuffixes[3],
            });
            if (filePath.empty()) {
                logHitNormalDiscoveryMiss();
                continue;  // 未找到则跳过该类型
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
            continue;  // 该类型全部 slot 加载失败
        }

        MM_LOG_INFO("Audio", "SFX loaded: type=%s slots=%d path=%s",
                    sfxName, loadedCount, filePath.c_str());
    }

    m_sfxLoaded = true;
    return true;
}

void AudioEngine::playSfx(SfxType type) {
    if (!m_initialized || !m_sfxLoaded) return;  // 前置条件不满足则静默返回
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(SfxType::Count)) return;  // 类型越界

    // 轮转 slot：每次 playSfx 使用下一个实例，快速连击不截断上一实例
    int slot = m_sfxRoundRobin[idx];                        // 当前轮到的 slot
    m_sfxRoundRobin[idx] = (slot + 1) % SFX_POOL_SIZE;      // 下次使用下一个 slot

    ma_sound* sound = m_sfxSounds[idx][slot];  // 取出该 slot 的 ma_sound
    if (!sound) {
        // 当前 slot 未加载，尝试其他 slot 作为回退
        for (int s = 0; s < SFX_POOL_SIZE; ++s) {
            if (m_sfxSounds[idx][s]) {
                sound = m_sfxSounds[idx][s];
                break;
            }
        }
        if (!sound) return;  // 该类型无任何可用实例
    }

    // 每次播放前 seek 到开头，确保从头播放完整音效
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_set_volume(sound, calcVolume(SoundType::Effect));  // 应用 Effect 分组音量
    ma_sound_start(sound);  // 触发播放（可与其它 slot 并发）
}

} // namespace melody_matrix::audio

#pragma once

// ──────────────────────────────────────────────────────
//  audio_engine.h — 音频引擎（miniaudio 封装）
//
//  功能：BGM 播放、选歌预览（淡入/循环）、音效池、分组音量。
//  ActiveSound 池管理多路并发；SFX 每类型 3 实例轮转防截断。
// ──────────────────────────────────────────────────────

#include <string>    // filePath 存储
#include <cstdint>   // int64_t
#include <vector>    // m_activeSounds 活动声音池
#include <cstring>   // std::memset（init 中清零 ma_engine）

// 前置声明 miniaudio，避免在头文件中包含头
struct ma_engine;
struct ma_sound;

namespace melody_matrix::audio {

// 音频类型 — 分组音量与并发策略预留
enum class SoundType : int {
    Preview = 0,   ///< 选歌预览
    BGM     = 1,   ///< 菜单/游戏 BGM
    Effect  = 2,   ///< 音效（判定、UI）
};

/// 音效类型 — playSfx() 索引
enum class SfxType : int {
    MenuClick  = 0,    ///< 菜单切换铺面
    MenuHit    = 1,    ///< 菜单确认/取消/开始
    HitNormal  = 2,    ///< 击打音（扫描 res/*_normal.* 或 *-hitnormal.*）
    SliderTick = 3,    ///< 滑条 tick（预留）
    Count      = 4     ///< 音效类型总数（数组边界）
};

// ── ActiveSound：活动声音槽（禁止拷贝，仅移动）──
struct ActiveSound {
    ma_sound*  sound           = nullptr;  ///< miniaudio 声音对象指针
    SoundType   type            = SoundType::Preview;  ///< 声音类型（BGM/预览/音效）
    float       fadeTimer       = 0.0f;    ///< 淡入/淡出已过去的时间（秒）
    float       fadeDuration    = 0.0f;    ///< 淡入/淡出总时长（秒）
    float       fadeStartVol    = 0.0f;    ///< 淡变起始音量
    float       fadeTargetVol   = 0.0f;    ///< 淡变目标音量
    bool        isFadingOut     = false;   ///< true = 淡出中，结束后释放
    bool        isFadingIn      = false;   ///< true = 淡入中
    int64_t     previewStartMs  = 0;     ///< 预览循环起点（毫秒）
    int64_t     previewEndMs    = 0;     ///< 预览结束点（0=播到文件尾）
    std::string filePath;                  ///< 源文件路径（预览去重用）

    ActiveSound() = default;

    ActiveSound(const ActiveSound&) = delete;
    ActiveSound& operator=(const ActiveSound&) = delete;

    /// 移动构造：转移 ma_sound 所有权
    ActiveSound(ActiveSound&& other) noexcept
        : sound(other.sound)
        , type(other.type)
        , fadeTimer(other.fadeTimer)
        , fadeDuration(other.fadeDuration)
        , fadeStartVol(other.fadeStartVol)
        , fadeTargetVol(other.fadeTargetVol)
        , isFadingOut(other.isFadingOut)
        , isFadingIn(other.isFadingIn)
        , previewStartMs(other.previewStartMs)
        , previewEndMs(other.previewEndMs)
    {
        other.sound = nullptr;  ///< 所有权已转移
    }

    ActiveSound& operator=(ActiveSound&& other) noexcept {
        if (this != &other) {
            release();  // 先释放自身持有的 sound
            sound           = other.sound;
            type            = other.type;
            fadeTimer       = other.fadeTimer;
            fadeDuration    = other.fadeDuration;
            fadeStartVol    = other.fadeStartVol;
            fadeTargetVol   = other.fadeTargetVol;
            isFadingOut    = other.isFadingOut;
            isFadingIn     = other.isFadingIn;
            previewStartMs  = other.previewStartMs;
            previewEndMs    = other.previewEndMs;
            other.sound = nullptr;
        }
        return *this;
    }

    void release();  ///< 停止并 uninit ma_sound（实现在 .cpp）

    ~ActiveSound() { release(); }  ///< 析构时自动释放
};

/// 音频引擎 — miniaudio 封装，支持多路、淡入淡出、预览循环、SFX 池
class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine() { shutdown(); }  ///< 析构时确保资源释放

    /// 初始化 miniaudio 引擎
    bool init();
    /// 释放所有声音与引擎
    void shutdown();

    // ── 游戏内单路 BGM（兼容旧接口）──
    bool playSong(const std::string& filePath);
    void pause();
    void resume();
    void stop();
    void setVolume(float volume);
    float volume() const { return m_volume; }

    // ── 选歌预览 ──
    void playPreview(const std::string& filePath,
                    int64_t startTimeMs     = 0,
                    float   fadeInDurationS  = 0.3f,
                    int64_t previewDurationMs = 60000);

    /// 停止全部声音；durationS>0 时淡出
    void stopWithFade(float durationS = 0.0f);

    /// 每帧驱动淡入淡出与预览循环（主循环必须调用）
    void update(float dt);

    /// 仅供同模块 Playhead 适配；返回内部 ma_engine*（可能为空）
    ma_engine* rawEngine() const { return m_engine; }

    bool isPlaying() const;

    /// 查询当前 BGM/预览的 PCM cursor 与采样率（无活动声音返回 false）
    bool queryPlaybackCursor(int64_t& outFrames, int32_t& outSampleRate) const;

    int64_t positionMs() const;
    int64_t durationMs() const;
    void seekTo(int64_t positionMs);

    // ── 分组音量 ──
    void setTypeVolume(SoundType type, float volume);
    float getTypeVolume(SoundType type) const;

    // ── 音效 ──
    /// 全量加载所有音效文件（非流式）。加载失败时记录警告但不中断。
    /// 重复调用安全：已加载则直接返回 true。
    bool loadSfx();

    /// 播放指定类型的音效。若未加载或类型越界则静默返回。
    void playSfx(SfxType type);

private:
    /// 创建 ma_sound；streaming 时使用 MA_SOUND_FLAG_STREAM（MP3 seek 安全）
    ma_sound* createSound(const std::string& filePath, bool streaming = true);
    void destroySound(ma_sound* sound);
    /// 实际音量 = 全局 × 分组
    float calcVolume(SoundType type) const;

private:
    static constexpr int MAX_ACTIVE_SOUNDS = 2;  ///< 最多 2 路并发 BGM/预览

    ma_engine* m_engine    = nullptr;  ///< miniaudio 引擎实例
    bool        m_initialized = false;  ///< 是否已成功 init

    std::vector<ActiveSound> m_activeSounds;  ///< 活动 BGM/预览声音池

    // 音量
    float m_volume = 1.0f;  ///< 全局主音量 [0,1]
    float m_typeVolumes[3] = {1.0f, 1.0f, 1.0f};  ///< Preview/BGM/Effect 分组音量

    static constexpr int SFX_POOL_SIZE = 3;  ///< 每类音效 3 实例轮转
    ma_sound* m_sfxSounds[static_cast<int>(SfxType::Count)][SFX_POOL_SIZE] = {};  ///< SFX 实例池
    int m_sfxRoundRobin[static_cast<int>(SfxType::Count)] = {};  ///< 每类下一个播放 slot
    bool m_sfxLoaded = false;  ///< loadSfx 是否已完成
};

} // namespace melody_matrix::audio

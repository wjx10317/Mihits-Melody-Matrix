#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <cstring>

// 前置声明 miniaudio，避免在头文件中包含头
struct ma_engine;
struct ma_sound;

namespace melody_matrix::audio {

// 音频类型枚举 —— 为未来多种音频类型共存和音量分组预留
enum class SoundType : int {
    Preview = 0,   // 选歌预览
    BGM     = 1,   // 菜单背景音乐
    Effect  = 2,   // 音效（判定音、UI音等）
};

// 音效类型枚举 —— 用于 playSfx() 选择播放的音效
enum class SfxType : int {
    MenuClick  = 0,    // 菜单点击（切换铺面）
    MenuHit    = 1,    // 菜单击中（选中模组/取消模组/开始游戏）
    HitNormal  = 2,    // 游戏内 tap 击中
    SliderTick = 3,    // slidertick（预留）
    Count      = 4
};

// ============================================================
// ActiveSound —— 池中管理的活动声音
// 禁止拷贝（避免 ma_sound* 浅拷贝），只允许移动
// ============================================================
struct ActiveSound {
    ma_sound*  sound           = nullptr;
    SoundType   type            = SoundType::Preview;
    float       fadeTimer       = 0.0f;
    float       fadeDuration    = 0.0f;
    float       fadeStartVol    = 0.0f;
    float       fadeTargetVol   = 0.0f;
    bool        isFadingOut     = false;   // true = 正在淡出并准备释放
    bool        isFadingIn      = false;   // true = 正在淡入
    int64_t     previewStartMs  = 0;     // 预览起始时间（ms）
    int64_t     previewEndMs    = 0;     // 预览结束时间（ms，0=播到结尾）
    std::string filePath;                  // 音频文件路径（用于判断是否是同一首歌）

    ActiveSound() = default;

    // 禁止拷贝
    ActiveSound(const ActiveSound&) = delete;
    ActiveSound& operator=(const ActiveSound&) = delete;

    // 允许移动
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
        other.sound = nullptr;  // 所有权转移
    }

    ActiveSound& operator=(ActiveSound&& other) noexcept {
        if (this != &other) {
            release();  // 释放当前持有的
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

    void release();  // 实现在 .cpp 中（需调用 ma_* 函数）

    ~ActiveSound() { release(); }
};

// ============================================================
// AudioEngine —— 封装 miniaudio，支持多路音频、淡入淡出、预览循环
// ============================================================
class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine() { shutdown(); }

    // 初始化 / 关闭
    bool init();
    void shutdown();

    // 播放控制（游戏内，单路，兼容原有接口）
    bool playSong(const std::string& filePath);
    void pause();
    void resume();
    void stop();
    void setVolume(float volume);  // 全局音量（兼容原有接口）
    float volume() const { return m_volume; }

    // 预览播放（选歌界面用）
    // filePath: 音频文件绝对路径
    // startTimeMs: 预览起始时间（ms），0=从头
    // fadeInDurationS: 淡入时长（秒），0=无淡入
    // previewDurationMs: 预览播放时长（ms），0=播到结尾后循环回 startTimeMs
    void playPreview(const std::string& filePath,
                    int64_t startTimeMs     = 0,
                    float   fadeInDurationS  = 0.3f,
                    int64_t previewDurationMs = 60000);

    // 停止所有声音（带淡出）
    // 如果 durationS > 0，执行淡出；否则立即停止
    void stopWithFade(float durationS = 0.0f);

    // 每帧更新（驱动淡入淡出 + 预览循环检测）
    // 必须在主循环每帧调用，dt 为帧间隔（秒）
    void update(float dt);

    // 查询
    bool isPlaying() const;  // 实现在 .cpp（需 ma_sound_is_playing）
    int64_t positionMs() const;//当前位置
    int64_t durationMs() const;//总时长

    // Seek 到指定位置（毫秒）
    void seekTo(int64_t positionMs);

    // ---- 音量分组控制（未来扩展）----
    void setTypeVolume(SoundType type, float volume);
    float getTypeVolume(SoundType type) const;

    // ---- 音效（SFX）----
    /// 全量加载所有音效文件（非流式）。加载失败时记录警告但不中断。
    /// 重复调用安全：已加载则直接返回 true。
    bool loadSfx();

    /// 播放指定类型的音效。若未加载或类型越界则静默返回。
    void playSfx(SfxType type);

private:
    // 内部：创建 ma_sound
    // streaming=true 使用 MA_SOUND_FLAG_STREAM（MP3 seek 安全）
    ma_sound* createSound(const std::string& filePath, bool streaming = true);

    // 内部：释放 ma_sound（正确方式：uninit + ma_free）
    void destroySound(ma_sound* sound);

    // 计算实际音量（分组音量 × 全局音量）
    float calcVolume(SoundType type) const;

private:
    static constexpr int MAX_ACTIVE_SOUNDS = 2;  // 最多2路并发

    ma_engine* m_engine    = nullptr;
    bool        m_initialized = false;

    std::vector<ActiveSound> m_activeSounds;

    // 音量
    float m_volume = 1.0f;                           // 全局音量
    float m_typeVolumes[3] = {1.0f, 1.0f, 1.0f}; // 按 SoundType 分组音量

    // 音效（SFX）—— 全量加载（非流式），每个类型维护 3 个实例轮转播放，
    // 避免快速连击时第二次播放截断第一次。
    static constexpr int SFX_POOL_SIZE = 3;
    ma_sound* m_sfxSounds[static_cast<int>(SfxType::Count)][SFX_POOL_SIZE] = {};
    int m_sfxRoundRobin[static_cast<int>(SfxType::Count)] = {};
    bool m_sfxLoaded = false;
};

} // namespace melody_matrix::audio


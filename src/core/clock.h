/**
 * @file clock.h
 * @brief 权威游戏时钟（音频驱动）
 *
 * 文件职责：
 *   定义以音频播放位置为主、SDL tick 插值为辅的歌曲时间时钟。
 *
 * 主要依赖：
 *   标准库 <atomic>、<cstdint>；实现依赖 SDL_GetTicks64（clock.cpp）。
 *
 * 在项目中的用法：
 *   Kernel 持有 Clock 实例；PlayingState 通过 AudioEngine 同步音频位置；
 *   判定与渲染均使用 interpolatedNowMs()，按键事件用 songTimeAtTickMs()。
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace melody_matrix::core {

/**
 * @brief 权威游戏时钟
 *
 * 绝对不要使用帧时间累积做判定！歌曲时间由音频 cursor 驱动，
 * 两次音频更新之间用 SDL tick 外推，避免双时钟漂移。
 */
class Clock {
public:
    /**
     * @brief 从音频引擎同步当前播放位置
     * @param audioFrameTimeMs 音频当前歌曲时间（毫秒）
     *
     * 由 AudioEngine 在每帧 update 开始时调用；仅在 cursor 前进时刷新插值锚点。
     */
    void syncFromAudio(int64_t audioFrameTimeMs);

    /**
     * @brief 获取当前歌曲时间（含用户校准偏移）
     * @return 毫秒，不含 tick 插值
     */
    int64_t nowMs() const;

    /**
     * @brief 获取插值后的歌曲时间
     * @return 毫秒，音频 cursor 两次更新之间用 SDL tick 外推
     */
    int64_t interpolatedNowMs() const;

    /**
     * @brief 将 SDL 事件 timestamp 换算为歌曲时间
     * @param tickMs SDL_GetTicks64 或扩展后的 64 位 tick
     * @return 该 tick 时刻对应的歌曲时间（毫秒）
     */
    int64_t songTimeAtTickMs(uint64_t tickMs) const;

    /**
     * @brief 设置用户全局偏移（设置菜单中的 offset）
     * @param offsetMs 偏移毫秒，可正可负
     */
    void setUserOffset(int64_t offsetMs);

    /** @brief 重置时钟（加载新歌曲时） */
    void reset();

    /** @brief 暂停插值（暂停菜单时歌曲时间冻结） */
    void pause();

    /** @brief 恢复插值（从暂停恢复时重新锚定 tick） */
    void resume();

private:
    /**
     * @brief 自上次锚点以来经过的 SDL 毫秒数
     * @return elapsed 毫秒
     */
    int64_t elapsedMsSinceAnchor() const;

    std::atomic<int64_t> m_audioTimeMs{0};  ///< 最近一次音频 cursor（原子，跨线程安全）
    int64_t m_userOffsetMs = 0;             ///< 用户校准偏移
    int64_t m_anchorAudioMs = 0;            ///< 插值锚点：音频时间
    uint64_t m_anchorTickMs = 0;            ///< 插值锚点：SDL tick
    bool m_paused = false;                  ///< 是否暂停插值
};

} // namespace melody_matrix::core

#pragma once

#include "gameplay/judge_strategy.h"
#include "beatmap/note.h"

#include <vector>
#include <array>
#include <memory>
#include <functional>

namespace melody_matrix::gameplay {

/// 判定结果的事件类型
struct NoteHitEvent {
    JudgmentResult result;   ///< Perfect 或 Good
    int64_t        time;     ///< 音符的时间
    int32_t        row;
    int32_t        col;
    int64_t        pressTime; ///< 按键实际按下的时间
};

struct NoteMissEvent {
    int64_t time;           ///< 音符的时间
    int32_t row;
    int32_t col;
};

/// Hold 尾部释放判定结果（与 Tap 相同 ± 窗口）
enum class HoldReleaseResult : uint8_t {
    Ignored = 0,   ///< 该列没有活跃 Hold
    Perfect = 1,
    Good    = 2,
    Miss    = 3,
};

/// Hold 尾部判定事件
struct HoldTailEvent {
    int32_t           col = 0;
    int32_t           row = 0;
    HoldReleaseResult result = HoldReleaseResult::Ignored;
    int64_t           holdEndMs = 0;
    int64_t           releaseMs = 0;
};

/// 单列判定队列
struct ColumnQueue {
    std::vector<beatmap::Note> notes;  ///< 该列的音符（按时间升序）
    size_t head = 0;                    ///< 当前头部索引

    bool finished() const { return head >= notes.size(); }
    const beatmap::Note& front() const { return notes[head]; }
    void advance() { ++head; }
    void reset() { head = 0; notes.clear(); }
};

/// 多列判定队列 — 每列独立维护判定状态。
/// 音符按 col 字段分发到对应列的子队列中。
/// 每列的头部指针独立前进，支持并行判定。
class JudgeQueue {
public:
    static constexpr int MAX_COLS = 8;

    JudgeQueue();

    /// 将音符分配到各列（按 col 字段分发，col 必须 < MAX_COLS）
    void loadNotes(const std::vector<beatmap::Note>& notes);

    /// 设置判定策略（默认：StandardJudgeStrategy）
    void setStrategy(std::unique_ptr<IJudgeStrategy> strategy);

    /// 每帧更新：检查所有列的自动 Miss
    void update(int64_t nowMs, float od);

    /// 在给定列上处理按键按下
    /// @param pressTimeMs  按键时间
    /// @param column       列索引 (0-based)
    /// @param od           难度参数
    JudgmentResult onKeyPress(int64_t pressTimeMs, int32_t column, float od);

    /// 在给定列上处理按键释放（Hold 判定用）
    /// @param releaseTimeMs  释放时间
    /// @param column         列索引
    /// @param od             难度参数
    HoldReleaseResult onKeyRelease(int64_t releaseTimeMs, int32_t column, float od);

    /// 获取指定列的活跃 Hold 音符（正在按住中），无则返回 nullptr
    const beatmap::Note* getActiveHold(int32_t column) const;

    /// 是否所有列都已完成
    bool finished() const;

    /// 重置为空状态
    void reset();

    // ── 渲染器访问接口 ──

    /// 获取指定列的队列（供渲染器遍历可见音符）
    const ColumnQueue& columnQueue(int32_t col) const;

    /// 获取总列数
    int32_t columnCount() const { return m_columnCount; }

    /// 获取所有音符总数
    size_t totalNotes() const;

    // ── 事件回调 ──
    std::function<void(const NoteHitEvent&)>  onHit;
    std::function<void(const NoteMissEvent&)> onMiss;
    /// Hold 尾部松手/超时判定
    std::function<void(const HoldTailEvent&)> onHoldTail;

private:
    HoldReleaseResult judgeHoldTailTiming(int64_t dt, float od) const;
    HoldReleaseResult commitHoldTail(int32_t column, int64_t releaseTimeMs, float od);
    void commitHit(int32_t column, JudgmentResult result, int64_t pressTimeMs);
    void emitMiss(int32_t column);

    std::array<ColumnQueue, MAX_COLS> m_columns;
    int32_t m_columnCount = 0;

    /// 每列的活跃 Hold 状态
    struct ActiveHoldState {
        bool     holding = false;        ///< 是否正在按住
        int64_t  pressTimeMs = 0;        ///< 按下时间
        int64_t  holdEndTimeMs = 0;      ///< Hold 结束时间
        int32_t  row = 0;               ///< Hold 的行号
        int32_t  col = 0;               ///< Hold 的列号
    };
    std::array<ActiveHoldState, MAX_COLS> m_activeHolds;

    std::unique_ptr<IJudgeStrategy> m_strategy;
};

} // namespace melody_matrix::gameplay

#pragma once

// ──────────────────────────────────────────────────────
//  judge_queue.h — 多列判定队列
//  每列独立维护音符队列与 Hold 状态；支持按键按下/释放判定、
//  自动 Miss、事件回调，供 PlayingState 与渲染层查询。
// ──────────────────────────────────────────────────────

#include "gameplay/judge_strategy.h"  // 判定策略接口与 JudgmentResult 枚举
#include "beatmap/note.h"             // beatmap::Note 音符结构体

#include <vector>      // std::vector：每列音符列表
#include <array>       // std::array：固定 MAX_COLS 列的队列数组
#include <memory>      // std::unique_ptr：判定策略所有权
#include <functional>  // std::function：事件回调

namespace melody_matrix::gameplay {

/// 判定结果的事件类型
struct NoteHitEvent {
    JudgmentResult result;   ///< Hit300 / Hit100 / Hit50
    int64_t        time;     ///< 音符的时间
    int32_t        row;      ///< 音符行索引
    int32_t        col;      ///< 音符列索引
    int64_t        pressTime; ///< 按键实际按下的时间
};

struct NoteMissEvent {
    int64_t time;           ///< 音符的时间
    int32_t row;            ///< 音符行索引
    int32_t col;            ///< 音符列索引
};

/// Hold 尾部释放判定结果（与 Tap 共用 Stable 300/100/50 窗口）
enum class HoldReleaseResult : uint8_t {
    Ignored = 0,   ///< 该列没有活跃 Hold
    Hit300  = 1,   ///< 尾部在 300 窗口内松手
    Hit100  = 2,   ///< 尾部在 100 窗口内松手
    Hit50   = 3,   ///< 尾部在 50 窗口内松手
    Miss    = 4,   ///< 尾部超出 50 窗口或超时
};

/// Hold 尾部判定事件
struct HoldTailEvent {
    int32_t           col = 0;              ///< Hold 列索引
    int32_t           row = 0;              ///< Hold 行索引
    HoldReleaseResult result = HoldReleaseResult::Ignored;  ///< 尾部判定结果
    int64_t           holdEndMs = 0;        ///< 谱面 Hold 结束时间
    int64_t           releaseMs = 0;        ///< 玩家松手时间
};

/// 单列判定队列
struct ColumnQueue {
    std::vector<beatmap::Note> notes;  ///< 该列的音符（按时间升序）
    size_t head = 0;                    ///< 当前头部索引（下一颗待判定音符）

    bool finished() const { return head >= notes.size(); }  ///< 是否已全部判定完
    const beatmap::Note& front() const { return notes[head]; }  ///< 队头音符（不越界调用）
    void advance() { ++head; }          ///< 消费队头，head 前移
    void reset() { head = 0; notes.clear(); }  ///< 清空队列并重置 head
};

/// 多列判定队列 — 每列独立维护判定状态。
/// 音符按 col 字段分发到对应列的子队列中。
/// 每列的头部指针独立前进，支持并行判定。
class JudgeQueue {
public:
    static constexpr int MAX_COLS = 8;  ///< 最大支持列数

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
    std::function<void(const NoteHitEvent&)>  onHit;      ///< Tap/Hold 头部击中
    std::function<void(const NoteMissEvent&)> onMiss;     ///< 音符过期 Miss
    /// Hold 尾部松手/超时判定
    std::function<void(const HoldTailEvent&)> onHoldTail;

private:
    HoldReleaseResult judgeHoldTailTiming(int64_t dt, float od) const;  ///< Hold 尾部窗口判定
    HoldReleaseResult commitHoldTail(int32_t column, int64_t releaseTimeMs, float od);  ///< 提交尾部并推进
    void commitHit(int32_t column, JudgmentResult result, int64_t pressTimeMs);  ///< Tap 击中提交
    void emitMiss(int32_t column);  ///< 过期 Miss 提交

    std::array<ColumnQueue, MAX_COLS> m_columns;  ///< 每列独立的音符队列
    int32_t m_columnCount = 0;  ///< 有效列数（最大 col+1）

    /// 每列的活跃 Hold 状态
    struct ActiveHoldState {
        bool     holding = false;        ///< 是否正在按住
        int64_t  pressTimeMs = 0;        ///< 按下时间
        int64_t  holdEndTimeMs = 0;      ///< Hold 结束时间
        int32_t  row = 0;               ///< Hold 的行号
        int32_t  col = 0;               ///< Hold 的列号
    };
    std::array<ActiveHoldState, MAX_COLS> m_activeHolds;  ///< 每列 Hold 状态

    std::unique_ptr<IJudgeStrategy> m_strategy;  ///< 判定窗口策略（可替换）
};

} // namespace melody_matrix::gameplay

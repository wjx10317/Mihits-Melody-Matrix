#pragma once

#include "gameplay/judge_strategy.h"
#include "beatmap/note.h"
#include "util/event_manager.h"

#include <vector>
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

/// 判定队列 — 维护全局判定队列。
/// 头部指针在击中/Miss 时前进；永不后退。
/// 使用 vector + 头部索引（而非 std::queue）以提高渲染器遍历效率。
class JudgeQueue {
public:
    JudgeQueue();

    /// 将音符加载到队列中（必须按时间升序排序）
    void loadNotes(const std::vector<beatmap::Note>& notes);

    /// 设置判定策略（默认：StandardJudgeStrategy）
    void setStrategy(std::unique_ptr<IJudgeStrategy> strategy);

    /// 每帧更新：检查自动 Miss（超过阈值的音符）
    void update(int64_t nowMs, float od);

    /// 在给定时间处理按键
    JudgmentResult onKeyPress(int64_t pressTimeMs, float od);

    /// 是否所有音符都已判定
    bool finished() const;

    /// 重置为空状态
    void reset();

    /// 获取当前头部索引（供渲染器使用）
    size_t headIndex() const { return m_head; }

    /// 获取总音符数
    size_t totalNotes() const { return m_notes.size(); }

    /// 获取指定索引的音符（供渲染器遍历）
    const beatmap::Note& noteAt(size_t idx) const { return m_notes[idx]; }

    // ── 事件回调（由 Kernel 在连接时设置）──
    std::function<void(const NoteHitEvent&)>  onHit;
    std::function<void(const NoteMissEvent&)> onMiss;

private:
    /// 发出击中事件并前进头部
    void commitHit(JudgmentResult result, int64_t pressTimeMs);

    /// 发出 Miss 事件并前进头部
    void emitMiss();

    std::vector<beatmap::Note> m_notes;
    size_t m_head = 0;
    std::unique_ptr<IJudgeStrategy> m_strategy;
};

} // namespace melody_matrix::gameplay

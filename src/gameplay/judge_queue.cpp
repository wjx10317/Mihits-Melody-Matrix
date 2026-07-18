// ──────────────────────────────────────────────────────
//  judge_queue.cpp — 多列判定队列实现
//  Tap/Hold 判定、Hold 尾部释放、过期自动 Miss、列分发与排序。
// ──────────────────────────────────────────────────────

#include "gameplay/judge_queue.h"  // 判定队列类与事件结构体声明
#include "util/logger.h"           // MM_LOG_* 日志宏

#include <cmath>       // std::abs：计算按键与音符的时间差绝对值
#include <algorithm>   // std::sort：各列音符按时间升序排列

namespace melody_matrix::gameplay {

// 构造函数：创建默认的 Stable 判定策略实例
JudgeQueue::JudgeQueue()
    : m_strategy(std::make_unique<StableJudgeStrategy>()) {}

/// 按 col 字段将音符分发到各列，并按时间升序排序
void JudgeQueue::loadNotes(const std::vector<beatmap::Note>& notes) {
    for (auto& col : m_columns) {
        col.reset();
    }
    m_activeHolds = {};
    m_columnCount = 0;

    for (const auto& note : notes) {
        if (note.col < 0 || note.col >= MAX_COLS) {
            MM_LOG_WARN("JudgeQueue", "Note with invalid col=" + std::to_string(note.col) + " skipped");
            continue;
        }
        m_columns[note.col].notes.push_back(note);
        if (note.col + 1 > m_columnCount) {
            m_columnCount = note.col + 1;
        }
    }

    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& col = m_columns[c];
        std::sort(col.notes.begin(), col.notes.end(),
                  [](const beatmap::Note& a, const beatmap::Note& b) { return a.time < b.time; });
    }

    size_t total = 0;
    for (int32_t c = 0; c < m_columnCount; ++c) {
        total += m_columns[c].notes.size();
    }
    MM_LOG_INFO("JudgeQueue", "Loaded " + std::to_string(total) +
                " notes across " + std::to_string(m_columnCount) + " columns");
}

/// 替换判定策略；传入 nullptr 则回退到 StableJudgeStrategy
void JudgeQueue::setStrategy(std::unique_ptr<IJudgeStrategy> strategy) {
    m_strategy = strategy ? std::move(strategy) : std::make_unique<StableJudgeStrategy>();
}

/// 提前松手相对 holdEnd 的时序窗（与 Tap 相同 Stable 公式）
HoldReleaseResult JudgeQueue::judgeHoldTailTiming(int64_t dt, float od) const {
    const int32_t w300 = m_strategy->hit300Window(od);
    const int32_t w100 = m_strategy->hit100Window(od);
    const int32_t w50  = m_strategy->hit50Window(od);
    const int64_t absDt = std::abs(dt);
    if (absDt <= w300) return HoldReleaseResult::Hit300;
    if (absDt <= w100) return HoldReleaseResult::Hit100;
    if (absDt <= w50)  return HoldReleaseResult::Hit50;
    return HoldReleaseResult::Miss;
}

/// 提交 Hold 尾部判定：固定结果、推进队列、触发 onHoldTail
HoldReleaseResult JudgeQueue::commitHoldTail(int32_t column, HoldReleaseResult result,
                                             int64_t releaseTimeMs) {
    auto& hold = m_activeHolds[column];
    if (!hold.holding) {
        return HoldReleaseResult::Ignored;
    }

    HoldTailEvent evt;
    evt.col = hold.col;
    evt.row = hold.row;
    evt.result = result;
    evt.holdEndMs = hold.holdEndTimeMs;
    evt.releaseMs = releaseTimeMs;

    hold.holding = false;
    auto& col = m_columns[column];
    if (!col.finished() && col.front().type == beatmap::NoteType::Hold) {
        col.advance();
    }

    if (onHoldTail) {
        onHoldTail(evt);
    }
    return result;
}

/// 每帧更新：按住到 holdEnd 自动 300；Tap 过期自动 Miss
void JudgeQueue::update(int64_t nowMs, float od) {
    const int64_t miss = m_strategy->missThreshold(od);

    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& hold = m_activeHolds[c];
        if (hold.holding && nowMs >= hold.holdEndTimeMs) {
            commitHoldTail(c, HoldReleaseResult::Hit300, hold.holdEndTimeMs);
        }
    }

    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& col = m_columns[c];
        while (!col.finished() && nowMs >= col.front().time + miss) {
            if (col.front().type == beatmap::NoteType::Hold && m_activeHolds[c].holding) {
                break;
            }
            emitMiss(c);
        }
    }
}

/// 列上按键按下：Tap 即时判定；Hold 进入 active 状态
JudgmentResult JudgeQueue::onKeyPress(int64_t pressTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) {
        MM_LOG_WARN("JudgeQueue", "onKeyPress: column " + std::to_string(column) +
                    " out of range [0," + std::to_string(m_columnCount) + ")");
        return JudgmentResult::Ignored;
    }

    auto& col = m_columns[column];
    if (col.finished()) {
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " finished (head=" + std::to_string(col.head) +
                     " size=" + std::to_string(col.notes.size()) + ")");
        return JudgmentResult::Ignored;
    }

    const auto& note = col.front();
    const int32_t w300 = m_strategy->hit300Window(od);
    const int32_t w100 = m_strategy->hit100Window(od);
    const int32_t w50  = m_strategy->hit50Window(od);
    const int64_t dt = pressTimeMs - note.time;
    const int64_t absDt = std::abs(dt);

    // 早于 50 窗外：不消耗，等待后续按键或自动 Miss
    if (dt < -static_cast<int64_t>(w50)) {
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " too early, dt=" + std::to_string(dt) + "ms" +
                     " w50=" + std::to_string(w50) + "ms" +
                     " noteTime=" + std::to_string(note.time) +
                     " pressTime=" + std::to_string(pressTimeMs));
        return JudgmentResult::Ignored;
    }

    auto classify = [&](JudgmentResult hitResult) {
        if (note.type == beatmap::NoteType::Tap) {
            commitHit(column, hitResult, pressTimeMs);
            return hitResult;
        }
        // Hold：按下只记录按住状态，head 不推进
        if (m_activeHolds[column].holding) {
            return JudgmentResult::Ignored;
        }
        auto& hold = m_activeHolds[column];
        hold.holding = true;
        hold.pressTimeMs = pressTimeMs;
        hold.holdEndTimeMs = note.holdEnd;
        hold.row = note.row;
        hold.col = note.col;
        if (onHit) {
            NoteHitEvent evt;
            evt.result    = hitResult;
            evt.time      = note.time;
            evt.row       = note.row;
            evt.col       = note.col;
            evt.pressTime = pressTimeMs;
            onHit(evt);
        }
        return hitResult;
    };

    if (note.type == beatmap::NoteType::Tap || note.type == beatmap::NoteType::Hold) {
        if (absDt <= w300) return classify(JudgmentResult::Hit300);
        if (absDt <= w100) return classify(JudgmentResult::Hit100);
        if (absDt <= w50)  return classify(JudgmentResult::Hit50);
        // 超出 50 但尚未过期：忽略本次按键，等 update() 自动 Miss
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " beyond hit50, dt=" + std::to_string(dt) + "ms" +
                     " w50=" + std::to_string(w50) + "ms");
        return JudgmentResult::Ignored;
    }

    return JudgmentResult::Ignored;
}

/// 列上按键释放：提前松手走 300/100/50/Miss；已到 holdEnd 则 300
HoldReleaseResult JudgeQueue::onKeyRelease(int64_t releaseTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) {
        return HoldReleaseResult::Ignored;
    }
    auto& hold = m_activeHolds[column];
    if (!hold.holding) {
        return HoldReleaseResult::Ignored;
    }
    if (releaseTimeMs >= hold.holdEndTimeMs) {
        return commitHoldTail(column, HoldReleaseResult::Hit300, releaseTimeMs);
    }
    const int64_t dt = releaseTimeMs - hold.holdEndTimeMs;  // 负值：提前
    return commitHoldTail(column, judgeHoldTailTiming(dt, od), releaseTimeMs);
}

const beatmap::Note* JudgeQueue::getActiveHold(int32_t column) const {
    if (column < 0 || column >= m_columnCount) {
        return nullptr;
    }
    if (!m_activeHolds[column].holding) {
        return nullptr;
    }
    auto& col = m_columns[column];
    if (col.finished() || col.notes.empty()) {
        return nullptr;
    }
    return &col.front();
}

bool JudgeQueue::finished() const {
    for (int32_t c = 0; c < m_columnCount; ++c) {
        if (!m_columns[c].finished()) {
            return false;
        }
    }
    return true;
}

void JudgeQueue::reset() {
    for (auto& col : m_columns) {
        col.reset();
    }
    m_activeHolds = {};
    m_columnCount = 0;
}

const ColumnQueue& JudgeQueue::columnQueue(int32_t col) const {
    return m_columns[col];
}

size_t JudgeQueue::totalNotes() const {
    size_t total = 0;
    for (int32_t c = 0; c < m_columnCount; ++c) {
        total += m_columns[c].notes.size();
    }
    return total;
}

void JudgeQueue::commitHit(int32_t column, JudgmentResult result, int64_t pressTimeMs) {
    const auto& note = m_columns[column].front();

    if (onHit) {
        NoteHitEvent evt;
        evt.result    = result;
        evt.time      = note.time;
        evt.row       = note.row;
        evt.col       = note.col;
        evt.pressTime = pressTimeMs;
        onHit(evt);
    }

    m_columns[column].advance();
}

void JudgeQueue::emitMiss(int32_t column) {
    const auto& note = m_columns[column].front();

    MM_LOG_DEBUG("JudgeQueue", "emitMiss: col=" + std::to_string(note.col) +
                 " row=" + std::to_string(note.row) +
                 " noteTime=" + std::to_string(note.time) + "ms");

    if (onMiss) {
        NoteMissEvent evt;
        evt.time = note.time;
        evt.row  = note.row;
        evt.col  = note.col;
        onMiss(evt);
    }

    m_columns[column].advance();
}

} // namespace melody_matrix::gameplay

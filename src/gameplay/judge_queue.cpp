#include "gameplay/judge_queue.h"
#include "util/logger.h"

#include <cmath>
#include <algorithm>

namespace melody_matrix::gameplay {

JudgeQueue::JudgeQueue()
    : m_strategy(std::make_unique<StandardJudgeStrategy>()) {}

void JudgeQueue::loadNotes(const std::vector<beatmap::Note>& notes) {
    // 清空所有列
    for (auto& col : m_columns) {
        col.reset();
    }
    m_activeHolds = {};
    m_columnCount = 0;

    // 按 col 分发到各列
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

    // 各列按时间排序
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

void JudgeQueue::setStrategy(std::unique_ptr<IJudgeStrategy> strategy) {
    m_strategy = strategy ? std::move(strategy) : std::make_unique<StandardJudgeStrategy>();
}

HoldReleaseResult JudgeQueue::judgeHoldTailTiming(int64_t dt, float od) const {
    const int32_t pw = m_strategy->perfectWindow(od);
    const int32_t gw = m_strategy->goodWindow(od);
    if (std::abs(dt) <= pw) return HoldReleaseResult::Perfect;
    if (std::abs(dt) <= gw) return HoldReleaseResult::Good;
    return HoldReleaseResult::Miss;
}

HoldReleaseResult JudgeQueue::commitHoldTail(int32_t column, int64_t releaseTimeMs, float od) {
    auto& hold = m_activeHolds[column];
    if (!hold.holding) {
        return HoldReleaseResult::Ignored;
    }

    const int64_t dt = releaseTimeMs - hold.holdEndTimeMs;
    const HoldReleaseResult result = judgeHoldTailTiming(dt, od);

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

void JudgeQueue::update(int64_t nowMs, float od) {
    const int64_t miss = m_strategy->missThreshold(od);

    // Hold 尾部超时：超过 holdEnd + miss 仍未松手，按尾部 Miss 处理（与 Tap 过期 Miss 一致）
    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& hold = m_activeHolds[c];
        if (hold.holding && nowMs > hold.holdEndTimeMs + miss) {
            commitHoldTail(c, nowMs, od);
        }
    }

    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& col = m_columns[c];
        // 自动 Miss：跳过已过期的音符
        while (!col.finished() && nowMs >= col.front().time + miss) {
            // 如果是 Hold 且正在按住中，不判定 Miss（等待释放或超时兜底）
            if (col.front().type == beatmap::NoteType::Hold && m_activeHolds[c].holding) {
                break;
            }
            emitMiss(c);
        }
    }
}

JudgmentResult JudgeQueue::onKeyPress(int64_t pressTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) {
        MM_LOG_WARN("JudgeQueue", "onKeyPress: column " + std::to_string(column) +
                    " out of range [0," + std::to_string(m_columnCount) + ")");
        return JudgmentResult::Ignored;
    }

    auto& col = m_columns[column];
    if (col.finished()) {
        MM_LOG_INFO("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                    " finished (head=" + std::to_string(col.head) +
                    " size=" + std::to_string(col.notes.size()) + ")");
        return JudgmentResult::Ignored;
    }

    const auto& note = col.front();
    const int32_t pw = m_strategy->perfectWindow(od);
    const int32_t gw = m_strategy->goodWindow(od);
    const int64_t dt = pressTimeMs - note.time;

    // 过早：不在 Good 窗口内
    if (dt < -static_cast<int64_t>(gw)) {
        MM_LOG_INFO("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                    " too early, dt=" + std::to_string(dt) + "ms" +
                    " gw=" + std::to_string(gw) + "ms" +
                    " noteTime=" + std::to_string(note.time) +
                    " pressTime=" + std::to_string(pressTimeMs));
        return JudgmentResult::Ignored;
    }

    // Tap 判定
    if (note.type == beatmap::NoteType::Tap) {
        if (std::abs(dt) <= pw) {
            commitHit(column, JudgmentResult::Perfect, pressTimeMs);
            return JudgmentResult::Perfect;
        }
        if (std::abs(dt) <= gw) {
            commitHit(column, JudgmentResult::Good, pressTimeMs);
            return JudgmentResult::Good;
        }
        // 在 Good 和 Miss 之间：忽略，等 update() 自动 Miss
        MM_LOG_INFO("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                    " between good and miss, dt=" + std::to_string(dt) + "ms" +
                    " gw=" + std::to_string(gw) + "ms");
        return JudgmentResult::Ignored;
    }

    // Hold 判定：按下时只进入 active 状态；释放/超时后才推进 head。
    if (note.type == beatmap::NoteType::Hold) {
        if (m_activeHolds[column].holding) {
            return JudgmentResult::Ignored;
        }
        if (std::abs(dt) <= pw) {
            // Perfect 按下 → 进入 Hold 状态
            auto& hold = m_activeHolds[column];
            hold.holding = true;
            hold.pressTimeMs = pressTimeMs;
            hold.holdEndTimeMs = note.holdEnd;
            hold.row = note.row;
            hold.col = note.col;
            if (onHit) {
                NoteHitEvent evt;
                evt.result    = JudgmentResult::Perfect;
                evt.time      = note.time;
                evt.row       = note.row;
                evt.col       = note.col;
                evt.pressTime = pressTimeMs;
                onHit(evt);
            }
            return JudgmentResult::Perfect;
        }
        if (std::abs(dt) <= gw) {
            auto& hold = m_activeHolds[column];
            hold.holding = true;
            hold.pressTimeMs = pressTimeMs;
            hold.holdEndTimeMs = note.holdEnd;
            hold.row = note.row;
            hold.col = note.col;
            if (onHit) {
                NoteHitEvent evt;
                evt.result    = JudgmentResult::Good;
                evt.time      = note.time;
                evt.row       = note.row;
                evt.col       = note.col;
                evt.pressTime = pressTimeMs;
                onHit(evt);
            }
            return JudgmentResult::Good;
        }
        return JudgmentResult::Ignored;
    }

    return JudgmentResult::Ignored;
}

HoldReleaseResult JudgeQueue::onKeyRelease(int64_t releaseTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) {
        return HoldReleaseResult::Ignored;
    }
    return commitHoldTail(column, releaseTimeMs, od);
}

const beatmap::Note* JudgeQueue::getActiveHold(int32_t column) const {
    if (column < 0 || column >= m_columnCount) {
        return nullptr;
    }
    if (!m_activeHolds[column].holding) {
        return nullptr;
    }
    // Hold 按下后 head 保持在该 Hold，释放/超时后才推进。
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

    MM_LOG_INFO("JudgeQueue", "emitMiss: col=" + std::to_string(note.col) +
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

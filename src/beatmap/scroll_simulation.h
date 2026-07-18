#pragma once

// ──────────────────────────────────────────────────────
//  scroll_simulation.h — 滚动窗口模拟公式（header-only）
//
//  用途：osu 转换期（OsuParser）与运行时（PlayingState）共用同一套
//  滚动时序/可达性公式，避免 gameplay 层与 beatmap 层循环依赖。
//
//  核心概念：
//    - 活跃列窗：宽度 kScrollActiveCols（4），在 totalCols 上滑动
//    - 滚动触发：max(lastTransitionEnd, targetTime - approachMs)
//    - 前向阻塞：窗内早于目标的 Tap/Hold 用 noteBlocksUntilMs 推迟 scrollStart
//    - 可达性：scrollEnd = scrollStart + duration ≤ earliestHit
//    - 解析器装不下：降级/丢前向阻塞（不丢目标）；仍不行则本次不滚
// ──────────────────────────────────────────────────────

#include "beatmap/note.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace melody_matrix::beatmap {

/// 活跃列数（dfjk 四键对应四列可见窗）
inline constexpr int kScrollActiveCols = 4;
/// 选择 scrollWindowStart 时的前瞻窗口（毫秒）
inline constexpr int64_t kScrollLookaheadMs = 520;
/// 滚动动画时长下限/上限（毫秒）
inline constexpr int64_t kScrollDurationMinMs = 100;
inline constexpr int64_t kScrollDurationMaxMs = 200;

// ── AR/OD 派生窗口 ──

/// 接近时间（毫秒）：max(300, 1800 - AR×120)
/// makeWindow / PlayingState / 滚动触发共用此公式。
inline int64_t approachMs(float ar) {
    return std::max<int64_t>(300, static_cast<int64_t>(1800.0f - ar * 120.0f));
}

/// Stable 最外层可判定半窗（50 窗，毫秒）：200 - 10×OD
inline int32_t hit50WindowMs(float od) {
    return static_cast<int32_t>(std::max(0.0, 200.0 - 10.0 * static_cast<double>(od)));
}

/// 兼容旧名
inline int32_t goodWindowMs(float od) {
    return hit50WindowMs(od);
}

/// 该 note 占用判定/列的截止时刻（变阵与滚动开滚前必须等过此点）
/// Tap：time + hit50；Hold：holdEnd（与运行时按住到 holdEnd 自动 300 对齐）
inline int64_t noteBlocksUntilMs(int64_t timeMs, bool isHold, int64_t holdEndMs, float od) {
    if (isHold) {
        return holdEndMs;
    }
    return timeMs + hit50WindowMs(od);
}

inline int64_t noteBlocksUntilMs(const Note& note, float od) {
    return noteBlocksUntilMs(note.time, note.isHold(), note.holdEnd, od);
}

/// 自适应滚动动画时长（毫秒）
/// 在 [scrollStart, targetTime) 内留 minRemaining = hit50+10，
/// 取可用时间的 80%，再 clamp 到 [100, 200]。
inline int64_t scrollDurationMs(int64_t targetNoteTimeMs, int64_t scrollStartMs, float od) {
    const int64_t minRemainingMs = static_cast<int64_t>(hit50WindowMs(od)) + 10;
    int64_t availableTime = targetNoteTimeMs - scrollStartMs - minRemainingMs;
    double duration = static_cast<double>(availableTime) * 0.8;
    duration = std::max(static_cast<double>(kScrollDurationMinMs),
                        std::min(static_cast<double>(kScrollDurationMaxMs), duration));
    return static_cast<int64_t>(duration);
}

/// 滚动触发时刻：变阵结束后与 approach 触发点二者取大
inline int64_t scrollTriggerMs(int64_t targetNoteTimeMs, int64_t lastTransitionEndMs, float ar) {
    return std::max(lastTransitionEndMs, targetNoteTimeMs - approachMs(ar));
}

/// Hold 尾部最晚判定（谱面窗口语义，仍为 holdEnd+hit50；开滚阻塞请用 noteBlocksUntilMs）
inline int64_t holdReleaseLatestHitMs(int64_t holdEndMs, float od) {
    return holdEndMs + hit50WindowMs(od);
}

// ── 前向 note 阻塞 scrollStart（Tap + Hold）──

/// 在 [windowStart, windowEnd] 内、早于目标 note 的 Tap/Hold
/// 将 scrollStart 推迟到各自 noteBlocksUntilMs。
inline int64_t scrollStartMsFromWindowNotes(int32_t windowStart, int32_t windowEnd,
                                            int64_t targetNoteTimeMs, int64_t scrollTriggerMs,
                                            float od,
                                            const std::vector<Note>& notes) {
    int64_t startMs = scrollTriggerMs;
    for (const auto& note : notes) {
        if (note.time >= targetNoteTimeMs) {
            break;
        }
        if (note.col < windowStart || note.col > windowEnd) {
            continue;
        }
        const int64_t blockUntil = noteBlocksUntilMs(note, od);
        if (blockUntil > scrollTriggerMs) {
            startMs = std::max(startMs, blockUntil);
        }
    }
    return startMs;
}

/// 兼容旧名
inline int64_t scrollStartMsFromWindowHolds(int32_t windowStart, int32_t windowEnd,
                                            int64_t targetNoteTimeMs, int64_t scrollTriggerMs,
                                            float od,
                                            const std::vector<Note>& notes) {
    return scrollStartMsFromWindowNotes(windowStart, windowEnd, targetNoteTimeMs,
                                        scrollTriggerMs, od, notes);
}

// ── 滚动可达性判定 ──

/// 滚动能否在 earliestHit 前完成
inline bool scrollCanReachNote(int64_t targetNoteTimeMs, int64_t earliestHitMs,
                               int64_t lastTransitionEndMs, float ar, float od,
                               int32_t windowStart, int32_t windowEnd,
                               const std::vector<Note>& notes) {
    const int64_t triggerMs = scrollTriggerMs(targetNoteTimeMs, lastTransitionEndMs, ar);
    const int64_t startMs = scrollStartMsFromWindowNotes(
        windowStart, windowEnd, targetNoteTimeMs, triggerMs, od, notes);
    if (startMs > earliestHitMs) {
        return false;
    }
    const int64_t endMs = startMs + scrollDurationMs(targetNoteTimeMs, startMs, od);
    return endMs <= earliestHitMs;
}

// ── 候选窗口起止 ──

inline int scrollCandidateMinStart(int targetCol, int keyCols = kScrollActiveCols) {
    return std::max(0, targetCol - keyCols + 1);
}

inline int scrollCandidateMaxStart(int targetCol, int totalCols, int keyCols = kScrollActiveCols) {
    const int maxStart = std::max(0, totalCols - keyCols);
    return std::min(targetCol, maxStart);
}

/// 在 [minStart, maxStart] 内选 windowStart：
/// 优先最大化 anchorTime 后 kScrollLookaheadMs 内落在窗内的 note 数；
/// 同分时选与 currentStart 距离最小的。
inline int chooseScrollWindowStart(int currentStart, int targetCol, int totalCols, int keyCols,
                                   int64_t anchorTimeMs,
                                   const std::vector<Note>& notes) {
    const int minStart = scrollCandidateMinStart(targetCol, keyCols);
    const int maxStart = scrollCandidateMaxStart(targetCol, totalCols, keyCols);
    int bestStart = std::max(minStart, std::min(currentStart, maxStart));
    int bestScore = -1;
    int bestDistance = std::abs(bestStart - currentStart);
    const int64_t lookaheadEnd = anchorTimeMs + kScrollLookaheadMs;

    for (int start = minStart; start <= maxStart; ++start) {
        const int end = start + keyCols - 1;
        int score = 0;
        for (const auto& note : notes) {
            if (note.time < anchorTimeMs || note.time > lookaheadEnd) {
                continue;
            }
            if (note.col >= start && note.col <= end) {
                ++score;
            }
        }
        const int distance = std::abs(start - currentStart);
        if (score > bestScore || (score == bestScore && distance < bestDistance)) {
            bestScore = score;
            bestDistance = distance;
            bestStart = start;
        }
    }
    return bestStart;
}

} // namespace melody_matrix::beatmap

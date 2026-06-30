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
//    - Hold 阻塞：窗内未完成 Hold 的 releaseLatestHit 推迟 scrollStart
//    - 可达性：scrollEnd = scrollStart + duration ≤ earliestHit
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

/// 接近时间（毫秒）：note 开始显示到击打时刻的提前量
/// 公式：max(300, 1800 - AR×120)，与 makeWindow / PlayingState 一致
inline int64_t approachMs(float ar) {
    // 计算 AR 对应的接近提前量，下限 300ms 防止过快
    return std::max<int64_t>(300, static_cast<int64_t>(1800.0f - ar * 120.0f));
}

/// Good 判定半窗（毫秒）：65 - 2.6×OD
inline int32_t goodWindowMs(float od) {
    // OD 越高判定窗越窄，结果不得为负
    return static_cast<int32_t>(std::max(0.0, 65.0 - 2.6 * od));
}

/// 自适应滚动动画时长（毫秒）
/// 在 [scrollStart, earliestHit) 内留 minRemaining = goodWindow+10，
/// 取可用时间的 80%，再 clamp 到 [100, 200]。
/// 与 PlayingState::checkAndTriggerScroll 一致。
inline int64_t scrollDurationMs(int64_t targetNoteTimeMs, int64_t scrollStartMs, float od) {
    // 滚动结束后至少保留 good 窗 + 10ms 缓冲，供玩家击打
    const int64_t minRemainingMs = static_cast<int64_t>(goodWindowMs(od)) + 10;
    // 从 scrollStart 到 target 时刻之间可用于动画的净时长
    int64_t availableTime = targetNoteTimeMs - scrollStartMs - minRemainingMs;
    // 取可用时间的 80% 作为动画时长（留余量避免卡边）
    double duration = static_cast<double>(availableTime) * 0.8;
    // 限制在 [100, 200] ms 区间内
    duration = std::max(static_cast<double>(kScrollDurationMinMs),
                        std::min(static_cast<double>(kScrollDurationMaxMs), duration));
    // 转回整数毫秒
    return static_cast<int64_t>(duration);
}

/// 滚动触发时刻：变阵结束后与 approach 触发点二者取大
inline int64_t scrollTriggerMs(int64_t targetNoteTimeMs, int64_t lastTransitionEndMs, float ar) {
    // 不能早于上次变阵/滚动结束；也不能早于 note 的 approach 显示触发点
    return std::max(lastTransitionEndMs, targetNoteTimeMs - approachMs(ar));
}

/// Hold 尾部最晚判定时刻 = holdEnd + goodWindow + 50（Miss 缓冲）
/// 与 parser makeWindow(endTime).latestHit 一致
inline int64_t holdReleaseLatestHitMs(int64_t holdEndMs, float od) {
    // holdEnd 之后仍允许 good+miss 判定，+50 对应 makeWindow 中的 miss 扩展
    return holdEndMs + goodWindowMs(od) + 50;
}

// ── Hold 阻塞 scrollStart ──

/// 在 [windowStart, windowEnd] 内、早于目标 note 的 Hold 若 releaseLatest
/// 晚于 scrollTrigger，则将 scrollStart 推迟到 releaseLatest。
inline int64_t scrollStartMsFromWindowHolds(int32_t windowStart, int32_t windowEnd,
                                            int64_t targetNoteTimeMs, int64_t scrollTriggerMs,
                                            float od,
                                            const std::vector<Note>& notes) {
    // 初始 scrollStart 等于 approach/变阵约束下的触发时刻
    int64_t startMs = scrollTriggerMs;
    // 遍历 notes（假定按 time 升序）
    for (const auto& note : notes) {
        // 只关心目标 note 之前的 Hold
        if (note.time >= targetNoteTimeMs) {
            break;
        }
        // 列不在当前活跃窗内则不影响滚动
        if (note.col < windowStart || note.col > windowEnd) {
            continue;
        }
        // 非 Hold 不阻塞
        if (!note.isHold()) {
            continue;
        }
        // 该 Hold 释放判定的最晚时刻
        const int64_t releaseLatest = holdReleaseLatestHitMs(note.holdEnd, od);
        // 若释放仍晚于 scrollTrigger，滚动须推迟到释放完毕
        if (releaseLatest > scrollTriggerMs) {
            startMs = std::max(startMs, releaseLatest);
        }
    }
    return startMs;
}

// ── 滚动可达性判定 ──

/// 滚动能否在 earliestHit 前完成：startMs ≤ earliestHit 且 endMs ≤ earliestHit
inline bool scrollCanReachNote(int64_t targetNoteTimeMs, int64_t earliestHitMs,
                               int64_t lastTransitionEndMs, float ar, float od,
                               int32_t windowStart, int32_t windowEnd,
                               const std::vector<Note>& notes) {
    // 计算无 Hold 阻塞时的滚动触发时刻
    const int64_t triggerMs = scrollTriggerMs(targetNoteTimeMs, lastTransitionEndMs, ar);
    // 叠加窗内 Hold 尾部阻塞后的实际 scrollStart
    const int64_t startMs = scrollStartMsFromWindowHolds(
        windowStart, windowEnd, targetNoteTimeMs, triggerMs, od, notes);
    // scrollStart 已超过最早可击打时刻 → 不可达
    if (startMs > earliestHitMs) {
        return false;
    }
    // 滚动结束时刻
    const int64_t endMs = startMs + scrollDurationMs(targetNoteTimeMs, startMs, od);
    // 结束也须在 earliestHit 之前才算可达
    return endMs <= earliestHitMs;
}

// ── 候选窗口起止 ──

/// 使 targetCol 落在窗内的最小 windowStart
inline int scrollCandidateMinStart(int targetCol, int keyCols = kScrollActiveCols) {
    // targetCol 在窗右端时 windowStart = targetCol - keyCols + 1
    return std::max(0, targetCol - keyCols + 1);
}

/// 使 targetCol 落在窗内的最大 windowStart（且不超过 totalCols - keyCols）
inline int scrollCandidateMaxStart(int targetCol, int totalCols, int keyCols = kScrollActiveCols) {
    // 窗不能超出总列数右边界
    const int maxStart = std::max(0, totalCols - keyCols);
    // targetCol 在窗左端时 windowStart = targetCol
    return std::min(targetCol, maxStart);
}

/// 在 [minStart, maxStart] 内选 windowStart：
/// 优先最大化 anchorTime 后 kScrollLookaheadMs 内落在窗内的 note 数；
/// 同分时选与 currentStart 距离最小的（减少无效滚动）。
inline int chooseScrollWindowStart(int currentStart, int targetCol, int totalCols, int keyCols,
                                   int64_t anchorTimeMs,
                                   const std::vector<Note>& notes) {
    // 使 targetCol 可见的 windowStart 合法区间
    const int minStart = scrollCandidateMinStart(targetCol, keyCols);
    const int maxStart = scrollCandidateMaxStart(targetCol, totalCols, keyCols);
    // 初始候选：将 currentStart clamp 到合法区间
    int bestStart = std::max(minStart, std::min(currentStart, maxStart));
    int bestScore = -1;  // 前瞻窗内 note 计数
    int bestDistance = std::abs(bestStart - currentStart);  // 与当前窗的距离
    // 前瞻窗口右边界
    const int64_t lookaheadEnd = anchorTimeMs + kScrollLookaheadMs;

    // 枚举所有合法 windowStart
    for (int start = minStart; start <= maxStart; ++start) {
        int score = 0;
        // 统计 [anchorTime, lookaheadEnd] 内落在 [start, start+keyCols) 的 note 数
        for (const auto& note : notes) {
            if (note.time < anchorTimeMs) {
                continue;
            }
            if (note.time > lookaheadEnd) {
                break;  // notes 升序，后续更晚可提前退出
            }
            if (note.col >= start && note.col < start + keyCols) {
                ++score;
            }
        }

        // 与 currentStart 的曼哈顿距离（越小滚动幅度越小）
        const int distance = std::abs(start - currentStart);
        // 优先高分；同分取距离更小者
        if (score > bestScore || (score == bestScore && distance < bestDistance)) {
            bestScore = score;
            bestDistance = distance;
            bestStart = start;
        }
    }

    return bestStart;
}

} // namespace melody_matrix::beatmap

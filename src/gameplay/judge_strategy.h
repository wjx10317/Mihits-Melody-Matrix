#pragma once

// ──────────────────────────────────────────────────────
//  judge_strategy.h — 判定策略
//  根据 OD 计算 Stable 规格 300/100/50 窗口与 Miss 阈值。
//  StableJudgeStrategy 采用 osu!stable (std) 线性公式。
// ──────────────────────────────────────────────────────

#include <algorithm>  // std::max
#include <cstdint>    // int32_t / int64_t / uint8_t

namespace melody_matrix::gameplay {

/// 判定结果类型（osu!stable：300 / 100 / 50 / Miss）
enum class JudgmentResult : uint8_t {
    Ignored = 0,   ///< 在任何窗口外按键 — 不消耗
    Hit300  = 1,   ///< 300 窗口内（Great）
    Hit100  = 2,   ///< 100 窗口内（OK）
    Hit50   = 3,   ///< 50 窗口内（Meh）
    Miss    = 4,   ///< 音符过期未被击中 / 超出 50 窗
};

/// 判定策略接口 — 根据 OD 确定时间窗口。
class IJudgeStrategy {
public:
    virtual ~IJudgeStrategy() = default;

    /// 300 窗口半宽（±毫秒）
    virtual int32_t hit300Window(float od) const = 0;

    /// 100 窗口半宽（±毫秒）
    virtual int32_t hit100Window(float od) const = 0;

    /// 50 窗口半宽（±毫秒）— 最外层可判定窗
    virtual int32_t hit50Window(float od) const = 0;

    /// Miss 阈值（毫秒）— 音符时间超过此值后自动 Miss
    virtual int64_t missThreshold(float od) const = 0;
};

/// Stable 判定策略（osu!stable / std 线性 OD 公式）
///
///   HitWindow300 = 80 - 6×OD
///   HitWindow100 = 140 - 8×OD
///   HitWindow50  = 200 - 10×OD
///
/// OD=0 → ±80 / ±140 / ±200 ms；OD=10 → ±20 / ±60 / ±100 ms。
/// 窗口明显大于 mania，便于暴露音画/输入不同步。
class StableJudgeStrategy : public IJudgeStrategy {
public:
    int32_t hit300Window(float od) const override {
        return static_cast<int32_t>(std::max(0.0f, 80.0f - 6.0f * od));
    }

    int32_t hit100Window(float od) const override {
        return static_cast<int32_t>(std::max(0.0f, 140.0f - 8.0f * od));
    }

    int32_t hit50Window(float od) const override {
        return static_cast<int32_t>(std::max(0.0f, 200.0f - 10.0f * od));
    }

    /// 超出 50 窗即 Miss（不再额外 +50ms mania 缓冲）
    int64_t missThreshold(float od) const override {
        return static_cast<int64_t>(hit50Window(od));
    }
};

/// 兼容旧名：现实现为 Stable 规格
using StandardJudgeStrategy = StableJudgeStrategy;

} // namespace melody_matrix::gameplay

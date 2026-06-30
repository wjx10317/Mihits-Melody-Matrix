#pragma once

// ──────────────────────────────────────────────────────
//  judge_strategy.h — 判定策略
//  根据 OD 计算 Perfect/Good 窗口与 Miss 阈值。
//  StandardJudgeStrategy 采用 osu!mania 风格线性公式。
// ──────────────────────────────────────────────────────

#include <cstdint>

namespace melody_matrix::gameplay {

/// 判定结果类型
enum class JudgmentResult : uint8_t {
    Ignored  = 0,   ///< 在任何窗口外按键 — 不消耗
    Perfect  = 1,   ///< 在完美窗口内
    Good     = 2,   ///< 在良好窗口内
    Miss     = 3,   ///< 音符过期未被击中
};

/// 判定策略接口 — 根据 OD 确定时间窗口。
class IJudgeStrategy {
public:
    virtual ~IJudgeStrategy() = default;

    /// 完美窗口（±毫秒）在给定 OD 值下
    virtual int32_t perfectWindow(float od) const = 0;

    /// 良好窗口（±毫秒）在给定 OD 值下
    virtual int32_t goodWindow(float od) const = 0;

    /// Miss 阈值（毫秒）— 音符时间超过此值后自动 Miss
    virtual int64_t missThreshold(float od) const = 0;
};

/// 标准判定策略（osu!mania 风格线性 OD 公式）
class StandardJudgeStrategy : public IJudgeStrategy {
public:
    /// Perfect 窗口：OD=0 时 ±22ms，OD=10 时 ±11.5ms
    int32_t perfectWindow(float od) const override {
        return static_cast<int32_t>(22.0f - 1.05f * od);
    }

    /// Good 窗口：OD=0 时 ±65ms，OD=10 时 ±39ms
    int32_t goodWindow(float od) const override {
        return static_cast<int32_t>(65.0f - 2.6f * od);
    }

    /// Miss 阈值：Good 窗口 + 50ms 缓冲后自动 Miss
    int64_t missThreshold(float od) const override {
        return static_cast<int64_t>(goodWindow(od)) + 50;
    }
};

} // namespace melody_matrix::gameplay

#pragma once

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
/// 实现类：StandardJudgeStrategy, EasyJudgeStrategy（模组）
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

/// 标准判定策略（遵循 osu!mania 风格公式）
class StandardJudgeStrategy : public IJudgeStrategy {
public:
    int32_t perfectWindow(float od) const override {
        // OD=0 时 ±22ms，线性缩放到 OD=10 时 ±11.5ms
        return static_cast<int32_t>(22.0f - 1.05f * od);
    }

    int32_t goodWindow(float od) const override {
        // OD=0 时 ±65ms，线性缩放到 OD=10 时 ±39ms
        return static_cast<int32_t>(65.0f - 2.6f * od);
    }

    int64_t missThreshold(float od) const override {
        // Good 窗口 + 50ms 自动 Miss 缓冲
        return goodWindow(od) + 50;
    }
};

/// 简单模组策略 — 更宽的窗口
class EasyJudgeStrategy : public IJudgeStrategy {
public:
    int32_t perfectWindow(float od) const override {
        return static_cast<int32_t>((22.0f - 1.05f * od) * 1.4f);
    }

    int32_t goodWindow(float od) const override {
        return static_cast<int32_t>((65.0f - 2.6f * od) * 1.4f);
    }

    int64_t missThreshold(float od) const override {
        return goodWindow(od) + 50;
    }
};

} // namespace melody_matrix::gameplay

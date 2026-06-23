#pragma once

#include "gameplay/judge_strategy.h"

#include <memory>
#include <cstdint>

namespace melody_matrix::gameplay {

/// HP 模组接口 — 允许覆盖 HP 行为（例如无失败模组）
class IHpMod {
public:
    virtual ~IHpMod() = default;

    /// 覆盖玩家是否死亡的判断。默认：检查 m_hp <= 0
    virtual bool isDead(float hp) const { return hp <= 0.0f; }

    /// 覆盖 HP 变化量。默认：返回不变的量
    virtual float modifyChange(float amount, JudgmentResult /*result*/) const { return amount; }
};

/// HP 管理器 — 跟踪玩家生命值（归一化 0~1）。
/// HP 根据判定结果变化。当 HP 为 0 时，游戏结束（除非启用无失败模组）。
class HPManager {
public:
    HPManager();

    /// 处理判定结果，相应地修改 HP
    void onJudgment(JudgmentResult result);

    /// 检查玩家是否死亡（HP ≤ 0，除非模组覆盖）
    bool isDead() const;

    /// 获取当前 HP（0.0 ~ 1.0）
    float hp() const { return m_hp; }

    /// 设置 HP 模组（例如无失败）
    void setMod(std::shared_ptr<IHpMod> mod);

    /// 设置 HP 消耗速率（来自难度）
    void setDrainRate(float hpDrain) { m_hpDrain = hpDrain; }

    /// 每帧应用被动 HP 消耗（基于难度和 dt）
    void drainPerFrame(float dtSeconds);

    /// 为新游玩重置
    void reset();

private:
    float m_hp = 1.0f;          ///< 当前 HP（0.0 ~ 1.0+）
    float m_hpDrain = 5.0f;     ///< 来自难度的 HP 消耗速率
    std::shared_ptr<IHpMod> m_mod;
};

/// 无失败模组 — 玩家永远不会死亡
class NoFailMod : public IHpMod {
public:
    bool isDead(float) const override { return false; }
};

} // namespace gameplay

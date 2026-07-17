// ──────────────────────────────────────────────────────
//  hp_manager.cpp — 生命值管理实现
//  判定增减 HP、按难度缩放变化量、每帧被动消耗。
// ──────────────────────────────────────────────────────

#include "gameplay/hp_manager.h"  // HPManager / IHpMod 声明
#include "util/logger.h"          // （预留）HP 相关日志

namespace melody_matrix::gameplay {

HPManager::HPManager() = default;  // 默认 HP=1.0，无模组

/// 根据判定结果计算 HP 变化，经难度与模组修正后 clamp 到 [0,1]
void HPManager::onJudgment(JudgmentResult result) {
    float change = 0.0f;  // 本次 HP 变化量（正=回血，负=扣血）

    switch (result) {
    case JudgmentResult::Hit300:
        change = 0.02f;    ///< 300 小幅回血
        break;
    case JudgmentResult::Hit100:
        change = 0.008f;   ///< 100 微量回血
        break;
    case JudgmentResult::Hit50:
        change = 0.002f;   ///< 50 几乎不回血
        break;
    case JudgmentResult::Miss:
        change = -0.10f;   ///< Miss 显著扣血
        break;
    default:
        return; ///< Ignored 不改变 HP
    }

    // 按难度 HP 消耗速率缩放（基准 HP=5）
    change *= (m_hpDrain / 5.0f);  // 难度越高，变化幅度越大

    // 模组可覆盖变化量（如无失败等）
    if (m_mod) {
        change = m_mod->modifyChange(change, result);
    }

    m_hp += change;  // 应用变化

    // 限制在 [0, 1] 区间
    if (m_hp < 0.0f) m_hp = 0.0f;
    if (m_hp > 1.0f) m_hp = 1.0f;
}

/// 是否死亡：优先询问模组，否则 HP ≤ 0
bool HPManager::isDead() const {
    if (m_mod) {
        return m_mod->isDead(m_hp);  // 模组可禁用死亡（如无失败）
    }
    return m_hp <= 0.0f;  // 默认：HP 耗尽即死亡
}

// 设置 HP 行为模组（如无失败）
void HPManager::setMod(std::shared_ptr<IHpMod> mod) {
    m_mod = std::move(mod);  // 接管模组所有权
}

/// 每帧被动 HP 消耗（HP=5 时约 0.5/秒）
void HPManager::drainPerFrame(float dtSeconds) {
    const float drainRate = m_hpDrain * 0.001f * dtSeconds;  // 帧间隔内的被动消耗量
    m_hp -= drainRate;
    if (m_hp < 0.0f) m_hp = 0.0f;  // 不低于 0
}

// 新局重置 HP 为满
void HPManager::reset() {
    m_hp = 1.0f;
}

} // namespace gameplay

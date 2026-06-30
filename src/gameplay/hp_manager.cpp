// ──────────────────────────────────────────────────────
//  hp_manager.cpp — 生命值管理实现
//  判定增减 HP、按难度缩放变化量、每帧被动消耗。
// ──────────────────────────────────────────────────────

#include "gameplay/hp_manager.h"
#include "util/logger.h"

namespace melody_matrix::gameplay {

HPManager::HPManager() = default;

/// 根据判定结果计算 HP 变化，经难度与模组修正后 clamp 到 [0,1]
void HPManager::onJudgment(JudgmentResult result) {
    float change = 0.0f;

    switch (result) {
    case JudgmentResult::Perfect:
        change = 0.02f;   ///< Perfect 小幅回血
        break;
    case JudgmentResult::Good:
        change = 0.005f;  ///< Good 微量回血
        break;
    case JudgmentResult::Miss:
        change = -0.10f;  ///< Miss 显著扣血
        break;
    default:
        return; ///< Ignored 不改变 HP
    }

    // 按难度 HP 消耗速率缩放（基准 HP=5）
    change *= (m_hpDrain / 5.0f);

    // 模组可覆盖变化量（如无失败等）
    if (m_mod) {
        change = m_mod->modifyChange(change, result);
    }

    m_hp += change;

    // 限制在 [0, 1] 区间
    if (m_hp < 0.0f) m_hp = 0.0f;
    if (m_hp > 1.0f) m_hp = 1.0f;
}

/// 是否死亡：优先询问模组，否则 HP ≤ 0
bool HPManager::isDead() const {
    if (m_mod) {
        return m_mod->isDead(m_hp);
    }
    return m_hp <= 0.0f;
}

void HPManager::setMod(std::shared_ptr<IHpMod> mod) {
    m_mod = std::move(mod);
}

/// 每帧被动 HP 消耗（HP=5 时约 0.5/秒）
void HPManager::drainPerFrame(float dtSeconds) {
    const float drainRate = m_hpDrain * 0.001f * dtSeconds;
    m_hp -= drainRate;
    if (m_hp < 0.0f) m_hp = 0.0f;
}

void HPManager::reset() {
    m_hp = 1.0f;
}

} // namespace gameplay

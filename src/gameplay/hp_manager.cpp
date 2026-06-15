#include "gameplay/hp_manager.h"
#include "util/logger.h"

namespace melody_matrix::gameplay {

HPManager::HPManager() = default;

void HPManager::onJudgment(JudgmentResult result) {
    float change = 0.0f;

    switch (result) {
    case JudgmentResult::Perfect:
        change = 0.02f;   // Small HP gain
        break;
    case JudgmentResult::Good:
        change = 0.005f;  // Tiny HP gain
        break;
    case JudgmentResult::Miss:
        change = -0.10f;  // Significant HP loss
        break;
    default:
        return; // Ignored — no HP change
    }

    // Scale by drain rate
    change *= (m_hpDrain / 5.0f);

    // Apply mod modification
    if (m_mod) {
        change = m_mod->modifyChange(change, result);
    }

    m_hp += change;

    // Clamp to [0, 1]
    if (m_hp < 0.0f) m_hp = 0.0f;
    if (m_hp > 1.0f) m_hp = 1.0f;
}

bool HPManager::isDead() const {
    if (m_mod) {
        return m_mod->isDead(m_hp);
    }
    return m_hp <= 0.0f;
}

void HPManager::setMod(std::shared_ptr<IHpMod> mod) {
    m_mod = std::move(mod);
}

void HPManager::drainPerFrame(float dtSeconds) {
    // Passive drain: ~0.5 HP per second at HP=5
    const float drainRate = m_hpDrain * 0.001f * dtSeconds;
    m_hp -= drainRate;
    if (m_hp < 0.0f) m_hp = 0.0f;
}

void HPManager::reset() {
    m_hp = 1.0f;
}

} // namespace gameplay

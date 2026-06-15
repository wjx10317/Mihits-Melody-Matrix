#include "gameplay/combo_manager.h"

#include <cmath>

namespace melody_matrix::gameplay {

void ComboManager::onHit() {
    ++m_combo;
    if (m_combo > m_maxCombo) {
        m_maxCombo = m_combo;
    }
}

void ComboManager::onMiss() {
    m_combo = 0;
}

float ComboManager::multiplier() const {
    // Combo multiplier formula: 1.0 + log2(combo + 1) * 0.2
    // This gives:
    //   combo=0  → mult=1.0
    //   combo=1  → mult=1.20
    //   combo=7  → mult=1.60
    //   combo=15 → mult=1.80
    return 1.0f + std::log2f(static_cast<float>(m_combo + 1)) * 0.2f;
}

void ComboManager::reset() {
    m_combo = 0;
    m_maxCombo = 0;
}

} // namespace melody_matrix::gameplay

// ──────────────────────────────────────────────────────
//  combo_manager.cpp — 连击管理实现
//  击中递增连击并更新峰值；Miss 归零；倍率采用 log2 曲线。
// ──────────────────────────────────────────────────────

#include "gameplay/combo_manager.h"  // ComboManager 类声明

#include <cmath>  // std::log2f：连击倍率计算

namespace melody_matrix::gameplay {

/// 击中（Perfect/Good）时递增连击，并刷新最大连击记录
void ComboManager::onHit() {
    ++m_combo;  // 当前连击 +1
    if (m_combo > m_maxCombo) {
        m_maxCombo = m_combo;  // 刷新本局最高连击
    }
}

/// Miss 时连击归零
void ComboManager::onMiss() {
    m_combo = 0;  // 断连，连击重置
}

/// 连击倍率：1.0 + log2(combo+1) × 0.2
/// 示例：combo=0→1.0，combo=1→1.20，combo=7→1.60，combo=15→1.80
float ComboManager::multiplier() const {
    return 1.0f + std::log2f(static_cast<float>(m_combo + 1)) * 0.2f;
}

/// 新局开始时重置连击与最大连击
void ComboManager::reset() {
    m_combo = 0;     // 当前连击归零
    m_maxCombo = 0;  // 最大连击记录清零
}

} // namespace melody_matrix::gameplay

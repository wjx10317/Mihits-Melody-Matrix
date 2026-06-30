#pragma once

// ──────────────────────────────────────────────────────
//  combo_manager.h — 连击管理
//  跟踪当前连击、最大连击，并提供连击倍率供计分使用。
//  由 JudgeQueue 的 onHit/onMiss 事件驱动，不直接绑定输入。
// ──────────────────────────────────────────────────────

#include <cstdint>

namespace melody_matrix::gameplay {

/// 连击管理器 — 跟踪当前和最大连击数。
/// 通过 JudgeQueue 的事件更新（非直接调用）。
class ComboManager {
public:
    /// 击中时调用（Perfect 或 Good）— 增加连击
    void onHit();

    /// Miss 时调用 — 重置连击为 0
    void onMiss();

    /// 获取当前连击数
    int32_t current() const { return m_combo; }

    /// 获取本次游玩达到的最大连击数
    int32_t max() const { return m_maxCombo; }

    /// 获取当前连击倍率（供 ScoreManager 使用）
    float multiplier() const;

    /// 为新游玩重置
    void reset();

private:
    int32_t m_combo   = 0;
    int32_t m_maxCombo = 0;
};

} // namespace melody_matrix::gameplay

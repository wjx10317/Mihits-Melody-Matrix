// ──────────────────────────────────────────────────────
//  score_manager.cpp — 分数计算实现
// ──────────────────────────────────────────────────────

#include "gameplay/score_manager.h"

#include <cmath>
#include <algorithm>

namespace melody_matrix::gameplay {

/// 单次击中得分：Stable 权重 300/100/50/0 × 连击倍率
int32_t ScoreManager::scoreForHit(JudgmentResult result, int32_t combo) {
    float acc = 0.0f;
    switch (result) {
    case JudgmentResult::Hit300: acc = 1.0f;                    break;  // 300/300
    case JudgmentResult::Hit100: acc = 100.0f / 300.0f;         break;
    case JudgmentResult::Hit50:  acc = 50.0f / 300.0f;          break;
    case JudgmentResult::Miss:   acc = 0.0f;                    break;
    default: return 0;
    }

    const float mult = 1.0f + std::log2f(static_cast<float>(combo + 1)) * 0.2f;
    return static_cast<int32_t>(BASE_SCORE * acc * mult);
}

void ScoreManager::addScore(JudgmentResult result, int32_t combo) {
    if (result == JudgmentResult::Ignored) return;

    m_totalScore += scoreForHit(result, combo);

    if (result != JudgmentResult::Miss) {
        ++m_hitNotes;
    }
    ++m_totalNotes;
}

float ScoreManager::accuracy() const {
    if (m_totalNotes == 0) return 1.0f;
    return static_cast<float>(m_totalNotes * BASE_SCORE) > 0 ?
        static_cast<float>(m_totalScore) / static_cast<float>(m_totalNotes * BASE_SCORE) : 1.0f;
}

void ScoreManager::recordNoteAvailable() {
}

void ScoreManager::reset() {
    m_totalScore  = 0;
    m_maxPossible = 0;
    m_totalNotes  = 0;
    m_hitNotes    = 0;
}

} // namespace melody_matrix::gameplay

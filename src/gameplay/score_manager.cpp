// ──────────────────────────────────────────────────────
//  score_manager.cpp — 分数计算实现
// ──────────────────────────────────────────────────────

#include "gameplay/score_manager.h"

#include <cmath>
#include <algorithm>

namespace melody_matrix::gameplay {

/// 单次击中的得分（静态，供预览与 addScore 共用）
int32_t ScoreManager::scoreForHit(JudgmentResult result, int32_t combo) {
    float acc = 0.0f;
    switch (result) {
    case JudgmentResult::Perfect: acc = 1.0f;    break;
    case JudgmentResult::Good:    acc = 0.6667f; break;
    case JudgmentResult::Miss:    acc = 0.3333f; break;
    default: return 0;
    }

    const float mult = 1.0f + std::log2f(static_cast<float>(combo + 1)) * 0.2f;
    return static_cast<int32_t>(BASE_SCORE * acc * mult);
}

/// 累加得分并更新命中统计（Miss 不计入 hitNotes）
void ScoreManager::addScore(JudgmentResult result, int32_t combo) {
    if (result == JudgmentResult::Ignored) return;

    m_totalScore += scoreForHit(result, combo);

    // Miss 不计入 hitNotes
    if (result != JudgmentResult::Miss) {
        ++m_hitNotes;
    }
    ++m_totalNotes;
}

/// 加权准确度：totalScore / (totalNotes × BASE_SCORE)
float ScoreManager::accuracy() const {
    if (m_totalNotes == 0) return 1.0f;
    // 加权准确度公式：totalScore / (totalNotes * BASE_SCORE)
    return static_cast<float>(m_totalNotes * BASE_SCORE) > 0 ?
        static_cast<float>(m_totalScore) / static_cast<float>(m_totalNotes * BASE_SCORE) : 1.0f;
}

/// 预留接口：最大可能分在 addScore 流程中维护
void ScoreManager::recordNoteAvailable() {
    // 不再使用 — m_totalNotes 在 addScore() 中维护
}

void ScoreManager::reset() {
    m_totalScore  = 0;
    m_maxPossible = 0;
    m_totalNotes  = 0;
    m_hitNotes    = 0;
}

} // namespace melody_matrix::gameplay

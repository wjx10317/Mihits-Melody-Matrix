#include "gameplay/formation_controller.h"
#include "util/logger.h"

#include <cmath>
#include <algorithm>

namespace melody_matrix::gameplay {

void FormationController::load(const std::vector<beatmap::Formation>& formations) {
    m_formations = formations;
    m_currentIndex = 0;

    // Ensure sorted by time
    std::sort(m_formations.begin(), m_formations.end(),
              [](const beatmap::Formation& a, const beatmap::Formation& b) {
                  return a.time < b.time;
              });

    if (!m_formations.empty()) {
        MM_LOG_INFO("FormationController", "Loaded " +
                    std::to_string(m_formations.size()) + " formations (" +
                    std::to_string(m_formations[0].rows) + "x" +
                    std::to_string(m_formations[0].cols) + " initial)");
    }
}

int32_t FormationController::currentCols() const {
    if (m_formations.empty()) return 4;
    return m_formations[m_currentIndex].cols;
}

bool FormationController::update(int64_t nowMs) {
    if (m_formations.empty()) return false;

    size_t newIndex = m_currentIndex;

    // Find the active formation at current time
    for (size_t i = 0; i < m_formations.size(); ++i) {
        if (m_formations[i].time <= nowMs) {
            newIndex = i;
        } else {
            break;
        }
    }

    if (newIndex != m_currentIndex) {
        // Formation transition triggered
        if (onFormationChanged) {
            FormationChangedEvent evt;
            evt.previous = m_formations[m_currentIndex];
            evt.current = m_formations[newIndex];
            evt.transitionStartMs = nowMs;
            evt.transitionEndMs = nowMs + m_formations[newIndex].transformDurationMs;
            onFormationChanged(evt);
        }

        m_currentIndex = newIndex;
        m_transitionStartMs = nowMs;
        MM_LOG_DEBUG("FormationController", "Formation changed to " +
                     std::to_string(m_formations[newIndex].rows) + "x" +
                     std::to_string(m_formations[newIndex].cols) + " at t=" +
                     std::to_string(nowMs) + "ms");
        return true;
    }

    return false;
}

float FormationController::transitionProgress(int64_t nowMs) const {
    if (m_transitionStartMs == 0) return 1.0f;

    int64_t durationMs = m_formations.empty() ? m_transitionDurationMs
                                                : m_formations[m_currentIndex].transformDurationMs;
    int64_t elapsed = nowMs - m_transitionStartMs;
    if (elapsed >= durationMs) return 1.0f;
    if (elapsed <= 0) return 0.0f;

    float t = static_cast<float>(elapsed) / static_cast<float>(durationMs);
    return easeInOutCubic(t);
}

bool FormationController::inTransition(int64_t nowMs) const {
    if (m_transitionStartMs == 0) return false;
    int64_t durationMs = m_formations.empty() ? m_transitionDurationMs
                                                : m_formations[m_currentIndex].transformDurationMs;
    return (nowMs - m_transitionStartMs) < durationMs;
}

void FormationController::cellToScreen(int row, int col, int rows, int cols,
                                        float& outX, float& outY) {
    // Logical resolution: 1920×1080
    const float W = 1920.0f, H = 1080.0f, margin = 120.0f;
    const float gw = (W - 2 * margin) / static_cast<float>(cols);
    const float gh = (H - 2 * margin) / static_cast<float>(rows);

    outX = margin + (col + 0.5f) * gw;
    outY = margin + (row + 0.5f) * gh;
}

void FormationController::reset() {
    m_currentIndex = 0;
    m_transitionStartMs = 0;
}

float FormationController::easeInOutCubic(float t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

} // namespace gameplay

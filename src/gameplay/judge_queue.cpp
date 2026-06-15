#include "gameplay/judge_queue.h"
#include "util/logger.h"

#include <cmath>
#include <algorithm>

namespace melody_matrix::gameplay {

JudgeQueue::JudgeQueue()
    : m_strategy(std::make_unique<StandardJudgeStrategy>()) {}

void JudgeQueue::loadNotes(const std::vector<beatmap::Note>& notes) {
    m_notes = notes;
    // Ensure sorted by time (should already be from BeatmapBuilder)
    std::sort(m_notes.begin(), m_notes.end(),
              [](const beatmap::Note& a, const beatmap::Note& b) { return a.time < b.time; });
    m_head = 0;
    MM_LOG_INFO("JudgeQueue", "Loaded " + std::to_string(m_notes.size()) + " notes");
}

void JudgeQueue::setStrategy(std::unique_ptr<IJudgeStrategy> strategy) {
    m_strategy = strategy ? std::move(strategy) : std::make_unique<StandardJudgeStrategy>();
}

void JudgeQueue::update(int64_t nowMs, float od) {
    const int64_t miss = m_strategy->missThreshold(od);

    // Auto-miss: advance head for notes that have expired
    while (m_head < m_notes.size() && nowMs >= m_notes[m_head].time + miss) {
        emitMiss();
    }
}

JudgmentResult JudgeQueue::onKeyPress(int64_t pressTimeMs, float od) {
    if (m_head >= m_notes.size()) {
        return JudgmentResult::Ignored; // No notes left
    }

    const auto& note = m_notes[m_head];
    const int32_t pw = m_strategy->perfectWindow(od);
    const int32_t gw = m_strategy->goodWindow(od);
    const int64_t dt = pressTimeMs - note.time; // Signed time difference

    // Early press: not yet in the good window — do not consume
    if (dt < -static_cast<int64_t>(gw)) {
        return JudgmentResult::Ignored;
    }

    // Hit detection (symmetric around note time)
    if (std::abs(dt) <= pw) {
        commitHit(JudgmentResult::Perfect, pressTimeMs);
        return JudgmentResult::Perfect;
    }
    if (std::abs(dt) <= gw) {
        commitHit(JudgmentResult::Good, pressTimeMs);
        return JudgmentResult::Good;
    }

    // Between good window and miss threshold: defensive — should have been caught by update()
    return JudgmentResult::Ignored;
}

bool JudgeQueue::finished() const {
    return m_head >= m_notes.size();
}

void JudgeQueue::reset() {
    m_notes.clear();
    m_head = 0;
}

void JudgeQueue::commitHit(JudgmentResult result, int64_t pressTimeMs) {
    const auto& note = m_notes[m_head];

    if (onHit) {
        NoteHitEvent evt;
        evt.result    = result;
        evt.time      = note.time;
        evt.row       = note.row;
        evt.col       = note.col;
        evt.pressTime = pressTimeMs;
        onHit(evt);
    }

    ++m_head;
}

void JudgeQueue::emitMiss() {
    const auto& note = m_notes[m_head];

    if (onMiss) {
        NoteMissEvent evt;
        evt.time = note.time;
        evt.row  = note.row;
        evt.col  = note.col;
        onMiss(evt);
    }

    ++m_head;
}

} // namespace melody_matrix::gameplay

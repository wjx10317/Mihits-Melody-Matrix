#include "beatmap/note_factory.h"
#include "util/logger.h"

namespace melody_matrix::beatmap {

NoteFactory::NoteFactory(std::shared_ptr<NotePool> pool)
    : m_pool(std::move(pool)) {
    if (!m_pool) {
        m_pool = std::make_shared<NotePool>();
    }
}

Note* NoteFactory::createTap(int64_t time, int32_t row, int32_t col) {
    Note* note = m_pool->acquire();
    if (!note) return nullptr;

    note->type = NoteType::Tap;
    note->time = time;
    note->row  = row;
    note->col  = col;
    return note;
}

Note* NoteFactory::createHold(int64_t startTime, int64_t endTime, int32_t row, int32_t col) {
    Note* note = m_pool->acquire();
    if (!note) return nullptr;

    note->type    = NoteType::Hold;
    note->time    = startTime;
    note->holdEnd = endTime;
    note->row     = row;
    note->col     = col;
    return note;
}

void NoteFactory::release(Note* note) {
    m_pool->release(note);
}

void NoteFactory::preAllocate(size_t count) {
    // If current pool is too small, we need to resize
    // For simplicity, we create a new larger pool and copy
    if (count > m_pool->capacity()) {
        MM_LOG_INFO("NoteFactory", "Pre-allocating pool from " +
                    std::to_string(m_pool->capacity()) + " to " +
                    std::to_string(count) + " slots");
        auto newPool = std::make_shared<NotePool>(count);
        m_pool = std::move(newPool);
    }
}

} // namespace melody_matrix::beatmap

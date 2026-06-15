#include "beatmap/note_pool.h"
#include "util/logger.h"

namespace melody_matrix::beatmap {

NotePool::NotePool(size_t capacity) {
    m_storage.resize(capacity);
    m_freeList.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        m_storage[i].inPool = true;
        m_freeList.push_back(i);
    }
    MM_LOG_DEBUG("NotePool", "Created pool with " + std::to_string(capacity) + " slots");
}

Note* NotePool::acquire() {
    if (m_freeList.empty()) {
        MM_LOG_WARN("NotePool", "Pool exhausted — cannot acquire note");
        return nullptr;
    }
    size_t idx = m_freeList.back();
    m_freeList.pop_back();
    m_storage[idx].inPool = false;
    m_storage[idx].reset(); // Clean state
    ++m_usedCount;
    return &m_storage[idx];
}

void NotePool::release(Note* note) {
    if (!note || note->inPool) return;

    // Calculate index from pointer (note must be in our storage)
    size_t idx = static_cast<size_t>(note - m_storage.data());
    if (idx >= m_storage.size()) {
        MM_LOG_ERROR("NotePool", "Release: note pointer not from this pool");
        return;
    }

    note->inPool = true;
    note->reset();
    m_freeList.push_back(idx);
    --m_usedCount;
}

void NotePool::reset() {
    m_freeList.clear();
    for (size_t i = 0; i < m_storage.size(); ++i) {
        m_storage[i].inPool = true;
        m_storage[i].reset();
        m_freeList.push_back(i);
    }
    m_usedCount = 0;
}

} // namespace melody_matrix::beatmap

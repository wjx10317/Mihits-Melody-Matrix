#pragma once

#include "beatmap/note.h"

#include <vector>
#include <cstddef>

namespace melody_matrix::beatmap {

/// Note 对象的固定容量对象池。
/// 构造时预分配所有音符；获取/释放操作都是 O(1)。
/// 非线程安全 — 仅从主线程访问。
class NotePool {
public:
    /// 创建一个具有 `capacity` 个预分配音符的池
    explicit NotePool(size_t capacity = 256);

    /// 不可复制
    NotePool(const NotePool&) = delete;
    NotePool& operator=(const NotePool&) = delete;

    /// 从池中获取一个音符。如果池耗尽返回 nullptr。
    Note* acquire();

    /// 将音符返回池中以供重用
    void release(Note* note);

    /// 重置池：所有音符恢复可用
    void reset();

    // ── 统计信息 ──
    size_t capacity() const { return m_storage.size(); }
    size_t used() const { return m_usedCount; }
    size_t available() const { return m_freeList.size(); }

private:
    /// 连续存储 — 无每个音符的堆分配
    std::vector<Note> m_storage;

    /// m_storage 的空闲索引栈
    std::vector<size_t> m_freeList;

    /// 当前已获取的音符数量
    size_t m_usedCount = 0;
};

} // namespace melody_matrix::beatmap

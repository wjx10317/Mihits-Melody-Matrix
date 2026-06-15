#pragma once

#include "beatmap/note.h"
#include "beatmap/note_pool.h"

#include <memory>

namespace melody_matrix::beatmap {

/// 从对象池创建 Note 对象的工厂。
/// 应用中的所有音符创建都应通过此工厂进行，
/// 以确保基于池的分配和正确的生命周期管理。
class NoteFactory {
public:
    /// 使用共享池构造工厂
    explicit NoteFactory(std::shared_ptr<NotePool> pool);

    /// 在给定时间和网格位置创建点音符
    /// 如果池耗尽返回 nullptr
    Note* createTap(int64_t time, int32_t row, int32_t col);

    /// 使用开始/结束时间和网格位置创建长按音符
    /// 如果池耗尽返回 nullptr
    Note* createHold(int64_t startTime, int64_t endTime, int32_t row, int32_t col);

    /// 将音符返回池
    void release(Note* note);

    /// 预分配池以处理 `count` 个音符
    void preAllocate(size_t count);

    /// 访问底层池
    NotePool& pool() { return *m_pool; }

private:
    std::shared_ptr<NotePool> m_pool;
};

} // namespace melody_matrix::beatmap

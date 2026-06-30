#pragma once

// ──────────────────────────────────────────────────────
//  note.h — 音符数据结构
//  谱面中单个击打事件：时间、网格坐标、Tap/Hold 类型。
// ──────────────────────────────────────────────────────

#include <cstdint>
#include <string>

namespace melody_matrix::beatmap {

/// 音符类型枚举
enum class NoteType : uint8_t {
    Tap = 0,    ///< 单点音符
    Hold = 1,   ///< 长按音符（按住）
};

/// 基础音符结构 — 谱面中的单个击打事件。
struct Note {
    int64_t  time = 0;        ///< 击打时间（毫秒，从歌曲开始算起）
    int32_t  row  = 0;        ///< 当前阵型中的行索引
    int32_t  col  = 0;        ///< 当前阵型中的列索引
    NoteType type = NoteType::Tap;

    // ── 长按音符字段（仅在 type == Hold 时有效）──
    int64_t  holdEnd = 0;     ///< 释放时间（毫秒）

    /// 长按音符的持续时间（点音符为 0）
    int64_t duration() const { return (type == NoteType::Hold) ? (holdEnd - time) : 0; }

    /// 检查此音符是否为长按音符
    bool isHold() const { return type == NoteType::Hold; }
};

} // namespace melody_matrix::beatmap

#pragma once

// ──────────────────────────────────────────────────────
//  beatmap.h — 谱面核心数据结构
//  Formation、Difficulty、Meta、Beatmap 及矩阵变换宏常量。
//  数据载体采用 struct（无业务逻辑，公开字段便于序列化与访问）。
// ──────────────────────────────────────────────────────

#include "beatmap/note.h"

#include <string>
#include <vector>
#include <cstdint>

namespace melody_matrix::beatmap {

/// ── 矩阵变换类型宏（v2）──
/// 对应 [FormationTransformMacros] 段定义的整数值。
/// 解析/序列化时直接使用 int32_t 存储，渲染层按数值分类分发动画。
/// 0   = MATRIX_TRANSFORM_NONE
/// 100 = MATRIX_TRANSFORM_SCALE_ONLY
/// 201~206 = SLIDE_ROW_ADD/REMOVE（_TOP/_BOTTOM/_BOTH）
/// 221~226 = SLIDE_COL_ADD/REMOVE（_LEFT/_RIGHT/_BOTH）
/// 299 = SLIDE_SINGLE_AXIS_COMPLEX
/// 301~304 = ROTATE_ROWS_COLS_ADD / ROWS_ADD_COLS_REMOVE / ROWS_REMOVE_COLS_ADD / ROWS_COLS_REMOVE
/// 399 = ROTATE_COMPLEX
namespace MatrixTransform {
    constexpr int32_t NONE                         = 0;
    constexpr int32_t SCALE_ONLY                   = 100;
    constexpr int32_t SLIDE_ROW_ADD_TOP            = 201;
    constexpr int32_t SLIDE_ROW_ADD_BOTTOM         = 202;
    constexpr int32_t SLIDE_ROW_ADD_BOTH           = 203;
    constexpr int32_t SLIDE_ROW_REMOVE_TOP         = 204;
    constexpr int32_t SLIDE_ROW_REMOVE_BOTTOM      = 205;
    constexpr int32_t SLIDE_ROW_REMOVE_BOTH        = 206;
    constexpr int32_t SLIDE_COL_ADD_LEFT           = 221;
    constexpr int32_t SLIDE_COL_ADD_RIGHT          = 222;
    constexpr int32_t SLIDE_COL_ADD_BOTH           = 223;
    constexpr int32_t SLIDE_COL_REMOVE_LEFT       = 224;
    constexpr int32_t SLIDE_COL_REMOVE_RIGHT      = 225;
    constexpr int32_t SLIDE_COL_REMOVE_BOTH       = 226;
    constexpr int32_t SLIDE_SINGLE_AXIS_COMPLEX    = 299;
    constexpr int32_t ROTATE_ROWS_COLS_ADD         = 301;
    constexpr int32_t ROTATE_ROWS_ADD_COLS_REMOVE  = 302;
    constexpr int32_t ROTATE_ROWS_REMOVE_COLS_ADD = 303;
    constexpr int32_t ROTATE_ROWS_COLS_REMOVE     = 304;
    constexpr int32_t ROTATE_COMPLEX               = 399;

    /// 判定宏类别：'N'=NONE, 'S'=SCALE, 'L'=SLIDE, 'R'=ROTATE
    inline char category(int32_t t) {
        if (t == NONE)                          return 'N';
        if (t == SCALE_ONLY)                     return 'S';
        if (t >= 201 && t <= 299)                return 'L';
        if (t >= 301 && t <= 399)                return 'R';
        return 'N';
    }
    inline bool isSlideRow(int32_t t)    { return t >= 201 && t <= 206; }
    inline bool isSlideCol(int32_t t)    { return t >= 221 && t <= 226; }
    inline bool isSlideRowAdd(int32_t t) { return t >= 201 && t <= 203; }
    inline bool isSlideRowRemove(int32_t t) { return t >= 204 && t <= 206; }
    inline bool isSlideColAdd(int32_t t) { return t >= 221 && t <= 223; }
    inline bool isSlideColRemove(int32_t t) { return t >= 224 && t <= 226; }
    inline bool isRotate(int32_t t)      { return t >= 301 && t <= 399; }
}

/// 阵型定义 — 描述特定时间点的网格布局（v2 标准6字段）
struct Formation {
    int64_t time = 0;                  ///< 此阵型生效的时间（毫秒）
    int32_t rows = 0;                  ///< 网格行数
    int32_t cols = 0;                  ///< 网格列数
    int32_t transformType = MatrixTransform::NONE;  ///< 矩阵变换类型宏（见 [FormationTransformMacros]）
    int64_t transformDurationMs = 0;   ///< 变换动画时长（毫秒），v2 短格式默认0=立即切换
    float blockSize = 1.0f;            ///< 格内内容缩放（底格满格；1.0=256px，0.9=缩小，1.1=放大）
};

/// 谱面难度参数
struct Difficulty {
    float hp   = 5.0f;   ///< HP 消耗速率（0-10）
    float od   = 5.0f;   ///< 总体难度（0-10，影响判定窗口）
    float ar   = 5.0f;   ///< 接近速率（0-10，影响接近时间）
    float star = 0.0f;   ///< 星级评定（计算得出，非解析）
};

/// 谱面元数据
struct Meta {
    std::string title;
    std::string artist;
    std::string creator;
    std::string version;   ///< 难度名称
    std::string audioFile; ///< 音频文件相对路径
    int64_t     previewTime = 0; ///< 预览点（毫秒）
};

/// 完整谱面数据 — BeatmapBuilder::build() 的输出
struct Beatmap {
    Meta                     meta;
    Difficulty               difficulty;
    std::vector<Formation>   formations;  ///< 按时间升序排列
    std::vector<Note>        notes;       ///< 按时间升序排列

    /// 查找给定时间点的活动阵型
    const Formation* formationAt(int64_t timeMs) const {
        const Formation* result = nullptr;
        for (const auto& f : formations) {
            if (f.time <= timeMs) {
                result = &f;
            } else {
                break;
            }
        }
        return result;
    }

    /// 获取总音符数
    size_t noteCount() const { return notes.size(); }

    /// 获取点音符数量
    size_t tapCount() const {
        size_t count = 0;
        for (const auto& n : notes) {
            if (n.type == NoteType::Tap) ++count;
        }
        return count;
    }

    /// 获取长按音符数量
    size_t holdCount() const {
        size_t count = 0;
        for (const auto& n : notes) {
            if (n.type == NoteType::Hold) ++count;
        }
        return count;
    }
};

} // namespace melody_matrix::beatmap
// 选型说明：均为数据载体 + const 查询，不涉及业务处理，故用 struct 而非 class，
// 避免大量冗余 getter/setter；默认公有访问更符合 POD 风格。

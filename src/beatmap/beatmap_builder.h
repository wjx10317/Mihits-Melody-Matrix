#pragma once

#include "beatmap/beatmap.h"
#include "util/result.h"
#include "util/error_codes.h"

#include <string>
#include <vector>

namespace melody_matrix::beatmap {

/// Beatmap 构建器 — 使用流式 API（链式 set/add 调用），
/// 然后调用 build() 进行集中验证（六条规则）。
///
/// 使用示例：
///   auto result = BeatmapBuilder()
///       .setMeta(meta)
///       .setDifficulty(diff)
///       .addFormation({0, 3, 4})
///       .addNote(Note{...})
///       .build();
class BeatmapBuilder {
public:
    // ── Fluent setters ──

    BeatmapBuilder& setMeta(const Meta& meta);
    BeatmapBuilder& mergeMeta(const Meta& meta);  ///< 将非空字段合并到现有元数据中
    BeatmapBuilder& setDifficulty(const Difficulty& diff);
    BeatmapBuilder& setFormatVersion(const std::string& version); ///< 设置来源格式版本（如 "MMA1"、"osu"）

    // ── Fluent adders ──

    BeatmapBuilder& addFormation(const Formation& formation);
    BeatmapBuilder& addNote(const Note& note);
    BeatmapBuilder& addFormations(const std::vector<Formation>& formations);
    BeatmapBuilder& addNotes(const std::vector<Note>& notes);

    // ── Build + validate ──

    /// 构建谱面。运行所有六条验证规则。
    /// 成功时返回 Beatmap，失败时返回错误。
    util::Result<Beatmap> build();

    /// 重置构建器状态以重用
    void reset();

private:
    // ── Validation helpers ──
    bool validateVersion() const;        ///< 规则 1：版本字符串检查（由解析器完成）
    bool validateAudioFile() const;      ///< 规则 2：音频文件路径非空
    bool validateNoteBounds() const;     ///< 规则 3：音符坐标在阵型边界内
    bool validateFormations() const;     ///< 规则 4：行数≥1 且列数≥1
    bool validateTimeOrder() const;      ///< 规则 5：阵型和音符按时间排序
    bool validateSections() const;       ///< 规则 6：必需段落存在
    bool validateHoldTapOverlap();       ///< 规则 7：同列 Hold+Tap 禁止重叠（软验证：丢弃冲突音符）
    bool validateFormationBuffer();      ///< 规则 8：阵型变化前 500ms 无即将判定的音符（软验证：丢弃冲突音符）

    // ── Builder state ──
    Meta                   m_meta;
    Difficulty             m_difficulty;
    std::vector<Formation> m_formations;
    std::vector<Note>      m_notes;
    std::string            m_formatVersion; ///< 由解析器设置（"MMA1"、"osu" 等）
};

} // namespace melody_matrix::beatmap

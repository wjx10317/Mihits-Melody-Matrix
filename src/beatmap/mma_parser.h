#pragma once

// ──────────────────────────────────────────────────────
//  mma_parser.h — 原生 .mma 谱面解析器（v2）
//  分段解析 General/Difficulty/Meta/Formations/Notes 等，
//  校验 transform 宏与 v2 规范约束。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap_parser.h"

#include <set>

namespace melody_matrix::beatmap {

/// 原生 .mma 谱面格式的解析器（v2）。
///
/// .mma v2 文件格式规范：
/// ─────────────────────────────────
/// 第 1 行："MMA1" 或 "MMA2"（格式版本，推荐 MMA2）
/// 空行和以 '#' 开头的行为注释
/// 段落由 [SectionName] 标记
///
/// 段落：
///   [General]
///     AudioFilename=<路径>
///     PreviewTime=<毫秒>
///
///   [Difficulty]
///     HP=<浮点数>
///     OD=<浮点数>
///     AR=<浮点数>
///
///   [Meta]
///     Title=<字符串>
///     Artist=<字符串>
///     Creator=<字符串>
///     Version=<字符串>
///
///   [FormationTransformMacros]  (v2 新增)
///     <宏名>=<整数值>
///     用于定义 transformType 的文件级宏。解析器只做校验，
///     Formation.transformType 直接以 int32 保存。
///     推荐宏值：0=NONE, 100=SCALE_ONLY,
///              201~206=SLIDE_ROW_ADD/REMOVE(_TOP/_BOTTOM/_BOTH),
///              221~226=SLIDE_COL_ADD/REMOVE(_LEFT/_RIGHT/_BOTH),
///              299=SLIDE_SINGLE_AXIS_COMPLEX,
///              301~304=ROTATE_ROWS_COLS_ADD/ROWS_ADD_COLS_REMOVE/ROWS_REMOVE_COLS_ADD/ROWS_COLS_REMOVE,
///              399=ROTATE_COMPLEX
///
///   [Formations]
///     v2 标准6字段：<时间>,<行数>,<列数>,<transformType>,<变换时长>,<块大小>
///     v2 短格式：<时间>,<行数>,<列数>  (transformType=0, transformDurationMs=0, blockSize=1.0)
///     兼容旧 MMA1 的 7 字段格式（第7位 noteTransformType 已废弃，忽略）
///     rows 范围 1-4，cols 范围 3-6
///
///   [Notes]
///     <时间>,<行>,<列>,<类型>
///     类型: T=点音符, H=长按
///     长按格式: <时间>,<行>,<列>,H,<结束时间>

class MmaParser : public BeatmapParser {
public:
    util::Result<void> parse(const std::string& content, BeatmapBuilder& builder) override;
    const char* formatName() const override { return "MMA"; }

private:
    /// 解析单个段落
    util::Result<void> parseGeneral(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseDifficulty(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseMeta(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseTransformMacros(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseFormations(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseNotes(const std::vector<std::string>& lines, BeatmapBuilder& builder);

    /// v2 校验辅助：[FormationTransformMacros] 定义的合法宏值集合（用于校验 transformType）
    std::set<int32_t> m_macroValues;   ///< 宏整数值集合
    bool m_hasMacros = false;          ///< 是否解析过宏段
    int64_t m_lastFormationTime = -1;  ///< 上一条 Formation 的 time（单调校验）
};

} // namespace melody_matrix::beatmap

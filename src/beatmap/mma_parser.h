#pragma once

#include "beatmap/beatmap_parser.h"

namespace melody_matrix::beatmap {

/// 原生 .mma 谱面格式的解析器。
///
/// .mma 文件格式规范：
/// ─────────────────────────────────
/// 第 1 行："MMA1"（格式版本）
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
///   [Formations]
///     <时间>,<行数>,<列数>[,<过渡类型>,<过渡时长>][,<块大小>,<note变换类型>]
///     过渡类型: 0=Fade(默认), 1=Insert, 2=Rotate
///     过渡时长: 毫秒(默认300)
///     块大小: 浮点，note图片相对格子的缩放比例(默认0.9留10%间距，1.0=占满)
///     note变换类型: 0=Scale缩放渐入(默认), 1=Fade淡入, 2=Rotate旋转入场
///     注意: 字段按顺序解析，跳过中间字段需显式写出默认值
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
    util::Result<void> parseFormations(const std::vector<std::string>& lines, BeatmapBuilder& builder);
    util::Result<void> parseNotes(const std::vector<std::string>& lines, BeatmapBuilder& builder);
};

} // namespace melody_matrix::beatmap

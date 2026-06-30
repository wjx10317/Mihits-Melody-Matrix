#pragma once

// ──────────────────────────────────────────────────────
//  beatmap_parser.h — 谱面解析器抽象接口
//  MmaParser / OsuParser 实现 parse()，由工厂按扩展名选择。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap_builder.h"
#include "util/result.h"

#include <string>
#include <memory>

namespace melody_matrix::beatmap {

/// 抽象解析器接口 — MmaParser 和 OsuParser 实现此接口。
/// 解析器仅读取并调用 builder.addXxx()，不执行最终验证。
class BeatmapParser {
public:
    virtual ~BeatmapParser() = default;

    /// 将给定文件内容解析到 BeatmapBuilder 中。
    /// 解析器负责：
    ///   - 读取段落数据并调用 builder.set*/add* 方法
    ///   - 不执行最终验证（这是 build() 的职责）
    ///
    /// @param content  文件完整内容（字符串形式）
    /// @param builder  要填充的构建器
    /// @return 成功或错误
    virtual util::Result<void> parse(const std::string& content, BeatmapBuilder& builder) = 0;

    /// 获取格式名称（例如 "MMA", "osu"）
    virtual const char* formatName() const = 0;
};

/// 工厂函数：检测文件格式并返回相应的解析器
/// 根据文件扩展名（.mma / .osu）创建对应解析器；未知则默认 MMA
std::unique_ptr<BeatmapParser> createParserForFile(const std::string& filename);

} // namespace melody_matrix::beatmap

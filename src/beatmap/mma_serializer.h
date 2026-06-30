#pragma once

// ──────────────────────────────────────────────────────
//  mma_serializer.h — .mma 谱面序列化
//  Beatmap → MMA2 文本；支持 source_hash 注释用于导入去重。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap.h"
#include "util/result.h"

#include <string>
#include <cstdint>

namespace melody_matrix::beatmap {

/// 将 Beatmap 对象序列化为 .mma 文件格式。
/// 输出文件头部包含 source_hash 注释行用于去重。
class MmaSerializer {
public:
    /// 将 Beatmap 序列化为 .mma 格式字符串
    /// @param beatmap 要序列化的铺面数据
    /// @param sourceHash 源文件 SHA256 哈希（留空则不写入 source_hash 注释）
    /// @return .mma 格式的完整文件内容
    static std::string serialize(const Beatmap& beatmap, const std::string& sourceHash = "");

    /// 将 Beatmap 序列化并写入文件
    /// @param beatmap 要序列化的铺面数据
    /// @param filePath 目标文件路径
    /// @param sourceHash 源文件 SHA256 哈希
    /// @return 成功或错误
    static util::Result<void> writeToFile(const Beatmap& beatmap,
                                          const std::string& filePath,
                                          const std::string& sourceHash = "");

    /// 从 .mma 文件头部提取 source_hash 注释
    /// @param filePath .mma 文件路径
    /// @return source_hash 值（64字符十六进制），不存在则返回空字符串
    static std::string readSourceHash(const std::string& filePath);
};

} // namespace melody_matrix::beatmap

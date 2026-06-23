#pragma once

#include "beatmap/note.h"

#include <string>
#include <vector>
#include <cstdint>

namespace melody_matrix::beatmap {

/// 阵型过渡类型
enum class TransitionType : int32_t {
    Fade    = 0,  ///< 交叉淡入淡出：旧网格淡出 + 新网格淡入 + 音符位置插值
    Insert  = 1,  ///< 渐入插入：现有行/列平滑缩放，新增行/列渐入
    Rotate  = 2,  ///< 矩阵旋转：前半段旧矩阵旋转到半角，切换，后半段新矩阵继续旋转
};

/// 阵型定义 — 描述特定时间点的网格布局
struct Formation {
    int64_t time = 0;                  ///< 此阵型生效的时间（毫秒）
    int32_t rows = 0;                  ///< 网格行数
    int32_t cols = 0;                  ///< 网格列数
    TransitionType transitionType = TransitionType::Fade;       ///< 过渡类型
    int64_t transitionDurationMs = 300; ///< 过渡持续时间（毫秒），0=瞬间切换
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
//为什么全用结构体？为什么这么选型？默认访问权限都是公有，类默认私有，类还要声明访问权限冗余，
// 另外这里都是数据载体外加const查询，不涉及业务处理，选择结构体。设置为私有要写大量冗余getset接口

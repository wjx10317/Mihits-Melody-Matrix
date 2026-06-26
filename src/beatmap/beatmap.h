#pragma once

#include "beatmap/note.h"

#include <string>
#include <vector>
#include <cstdint>

namespace melody_matrix::beatmap {

/// 矩阵变换方式（控制行列变化时的动画类型）
enum class MatrixTransformType : int32_t {
    Scale       = 0,  ///< 缩放：行列不变，仅格子大小平滑变换
    Slide       = 1,  ///< 滑入：新行从左滑入/新列从顶部滑下
    Rotate      = 2,  ///< 旋转一周：半周时变换矩阵，半周归位
    SlideOut    = 3,  ///< 滑出+旋转：行列减少时同时滑出和旋转
    ScaleSlide  = 4,  ///< 先缩放后滑入（大小+行列同时变化，单行/列添加）
    ScaleRotate = 5,  ///< 先缩放后旋转（大小+行列同时变化，多行/列添加）
};

/// note 图片出现方式（独立于矩阵过渡，控制单个note贴图的入场动画）
enum class NoteTransformType : int32_t {
    Scale  = 0,  ///< 缩放渐入（默认）：note从0缩放到blockSize
    Fade   = 1,  ///< 淡入：note透明度从0到1，尺寸不变
    Rotate = 2,  ///< 旋转入场：note从-90°旋转到0°同时缩放
};

/// 阵型定义 — 描述特定时间点的网格布局
struct Formation {
    int64_t time = 0;                  ///< 此阵型生效的时间（毫秒）
    int32_t rows = 0;                  ///< 网格行数
    int32_t cols = 0;                  ///< 网格列数
    MatrixTransformType transformType = MatrixTransformType::Scale;  ///< 矩阵变换方式
    int64_t transformDurationMs = 500; ///< 矩阵变换动画持续时间（毫秒），固定500ms
    float blockSize = 0.9f;            ///< note图片相对格子的缩放比例（1.0=占满格子，0.9=90%留间距）
    NoteTransformType noteTransformType = NoteTransformType::Scale;  ///< note图片出现方式
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

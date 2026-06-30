#pragma once

// ============================================================
// grid_layout.h — 判定矩阵屏幕布局
// 将 (row, col) 逻辑格坐标映射为 1920×1080 屏幕像素中心。
// 逻辑格宽 gw/gh 固定（256px），blockSize 仅缩放格内内容（note/background），
// 不改变格间距；scrollOffset 在列滚动动画时水平平移整列。
// ============================================================

#include <algorithm>  // std::max
#include <cstdint>    // int32_t

namespace melody_matrix::renderer {

/// 判定矩阵布局计算器。
/// 所有 note / 格子背景 / 缩圈共用同一套坐标系，避免滚动或缩放时出现错位。
struct GridLayout {
    static constexpr float kScreenW = 1920.0f;        ///< 逻辑屏幕宽度（像素）
    static constexpr float kScreenH = 1080.0f;        ///< 逻辑屏幕高度（像素）
    static constexpr float kMargin = 80.0f;           ///< 矩阵距屏幕底边的留白
    /// 整体下移矩阵（像素，屏幕 Y 向下），为顶部缩圈外扩留出空间
    static constexpr float kMatrixShiftY = 70.0f;
    /// 逻辑格宽/高（像素）。滚动偏移 scrollOffset 以此为步长。
    static constexpr float kDefaultCellW = 256.0f;
    static constexpr float kDefaultCellH = 256.0f;

    int32_t rows = 0;      ///< 当前阵型行数
    int32_t cols = 0;      ///< 当前阵型列数
    /// Formation.blockSize：格内 note/background 纹理缩放，1.0=铺满逻辑格
    float blockSize = 1.0f;

    /// 逻辑格宽（固定，不受 blockSize 影响）
    float gw() const {
        return kDefaultCellW;  // 水平方向每列占用的像素步长
    }

    /// 逻辑格高（固定，不受 blockSize 影响）
    float gh() const {
        return kDefaultCellH;  // 垂直方向每行占用的像素步长
    }

    /// 格内内容宽度 = gw × blockSize
    float contentW() const {
        return gw() * blockSize;  // note/background 实际绘制宽度
    }

    /// 格内内容高度 = gh × blockSize
    float contentH() const {
        return gh() * blockSize;  // note/background 实际绘制高度
    }

    /// 矩阵总宽 = cols × gw
    float matrixW() const {
        return static_cast<float>(std::max(cols, 0)) * gw();  // 整矩阵水平跨度
    }

    /// 矩阵总高 = rows × gh
    float matrixH() const {
        return static_cast<float>(std::max(rows, 0)) * gh();  // 整矩阵垂直跨度
    }

    /// 活跃列带宽度（列数，非像素）
    float activeWidth(int32_t activeStartCol, int32_t activeEndCol) const {
        // activeEndCol - activeStartCol + 1 为可见判定窗列数，至少为 1
        return static_cast<float>(std::max(activeEndCol - activeStartCol + 1, 1));
    }

    /// 活跃列带中心相对于 activeStartCol 的列偏移（用于居中）
    float activeCenterOffset(int32_t activeStartCol, int32_t activeEndCol) const {
        // 将活跃窗中心对齐到屏幕水平中心时的列索引偏移量
        return (activeWidth(activeStartCol, activeEndCol) - 1.0f) * 0.5f;
    }

    /// 活跃列带在屏幕上的左右 X 边界（像素）
    void activeBandX(int32_t activeStartCol, int32_t activeEndCol,
                     float& outLeft, float& outRight) const {
        const float width = activeWidth(activeStartCol, activeEndCol) * gw();  // 活跃带像素宽
        outLeft = kScreenW * 0.5f - width * 0.5f;   // 左边界：屏幕中心左移半宽
        outRight = outLeft + width;                    // 右边界：左边界 + 带宽
    }

    /// 计算 (row, col) 逻辑格中心在屏幕上的像素坐标。
    /// scrollOffset：列滚动动画时的水平偏移（负值=矩阵向左移）。
    void cellCenter(int32_t row, int32_t col,
                    int32_t activeStartCol, int32_t activeEndCol,
                    float scrollOffset,
                    float& outX, float& outY) const {
        const float centerOffset = activeCenterOffset(activeStartCol, activeEndCol);
        // X：以屏幕中心为基准，按列距 activeStartCol 的偏移排列，再叠加滚动
        outX = kScreenW * 0.5f
            + (static_cast<float>(col - activeStartCol) - centerOffset) * gw()
            + scrollOffset;
        // Y：自底向上排 row，kMargin 留底边，kMatrixShiftY 整体下移
        outY = kScreenH - kMargin - (static_cast<float>(row) + 0.5f) * gh()
            + kMatrixShiftY;
    }

    /// 粗略可见性裁剪：格心 ± 半格是否在屏幕内
    bool cellVisible(float cellX, float cellY) const {
        const float halfW = gw() * 0.5f;  // 逻辑格半宽
        const float halfH = gh() * 0.5f;  // 逻辑格半高
        // 四边任一与 [0, kScreenW/H] 有交集即视为可见
        return cellX + halfW >= 0.0f && cellX - halfW <= kScreenW
            && cellY + halfH >= 0.0f && cellY - halfH <= kScreenH;
    }
};

} // namespace melody_matrix::renderer

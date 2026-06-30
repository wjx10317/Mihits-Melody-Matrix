#pragma once

// ============================================================
// grid_layout.h — 判定矩阵屏幕布局
// 将 (row, col) 逻辑格坐标映射为 1920×1080 屏幕像素中心。
// 逻辑格宽 gw/gh 固定（256px），blockSize 仅缩放格内内容（note/background），
// 不改变格间距；scrollOffset 在列滚动动画时水平平移整列。
// ============================================================

#include <algorithm>
#include <cstdint>

namespace melody_matrix::renderer {

/// 判定矩阵布局计算器。
/// 所有 note / 格子背景 / 缩圈共用同一套坐标系，避免滚动或缩放时出现错位。
struct GridLayout {
    static constexpr float kScreenW = 1920.0f;
    static constexpr float kScreenH = 1080.0f;
    static constexpr float kMargin = 80.0f;
    /// 整体下移矩阵（像素，屏幕 Y 向下），为顶部缩圈外扩留出空间
    static constexpr float kMatrixShiftY = 70.0f;
    /// 逻辑格宽/高（像素）。滚动偏移 scrollOffset 以此为步长。
    static constexpr float kDefaultCellW = 256.0f;
    static constexpr float kDefaultCellH = 256.0f;

    int32_t rows = 0;
    int32_t cols = 0;
    /// Formation.blockSize：格内 note/background 纹理缩放，1.0=铺满逻辑格
    float blockSize = 1.0f;

    /// 逻辑格宽（固定，不受 blockSize 影响）
    float gw() const { return kDefaultCellW; }
    /// 逻辑格高（固定，不受 blockSize 影响）
    float gh() const { return kDefaultCellH; }
    /// 格内内容宽度 = gw × blockSize
    float contentW() const { return gw() * blockSize; }
    /// 格内内容高度 = gh × blockSize
    float contentH() const { return gh() * blockSize; }
    /// 矩阵总宽 = cols × gw
    float matrixW() const { return static_cast<float>(std::max(cols, 0)) * gw(); }
    /// 矩阵总高 = rows × gh
    float matrixH() const { return static_cast<float>(std::max(rows, 0)) * gh(); }

    /// 活跃列带宽度（列数）
    float activeWidth(int32_t activeStartCol, int32_t activeEndCol) const {
        return static_cast<float>(std::max(activeEndCol - activeStartCol + 1, 1));
    }

    /// 活跃列带中心相对于 activeStartCol 的列偏移（用于居中）
    float activeCenterOffset(int32_t activeStartCol, int32_t activeEndCol) const {
        return (activeWidth(activeStartCol, activeEndCol) - 1.0f) * 0.5f;
    }

    /// 活跃列带在屏幕上的左右 X 边界（像素）
    void activeBandX(int32_t activeStartCol, int32_t activeEndCol,
                     float& outLeft, float& outRight) const {
        const float width = activeWidth(activeStartCol, activeEndCol) * gw();
        outLeft = kScreenW * 0.5f - width * 0.5f;
        outRight = outLeft + width;
    }

    /// 计算 (row, col) 逻辑格中心在屏幕上的像素坐标。
    /// scrollOffset：列滚动动画时的水平偏移（负值=矩阵向左移）。
    void cellCenter(int32_t row, int32_t col,
                    int32_t activeStartCol, int32_t activeEndCol,
                    float scrollOffset,
                    float& outX, float& outY) const {
        const float centerOffset = activeCenterOffset(activeStartCol, activeEndCol);
        outX = kScreenW * 0.5f
            + (static_cast<float>(col - activeStartCol) - centerOffset) * gw()
            + scrollOffset;
        outY = kScreenH - kMargin - (static_cast<float>(row) + 0.5f) * gh()
            + kMatrixShiftY;
    }

    /// 粗略可见性裁剪：格心 ± 半格是否在屏幕内
    bool cellVisible(float cellX, float cellY) const {
        const float halfW = gw() * 0.5f;
        const float halfH = gh() * 0.5f;
        return cellX + halfW >= 0.0f && cellX - halfW <= kScreenW
            && cellY + halfH >= 0.0f && cellY - halfH <= kScreenH;
    }
};

} // namespace melody_matrix::renderer

#pragma once

#include <algorithm>
#include <cstdint>

namespace melody_matrix::renderer {

struct GridLayout {
    static constexpr float kScreenW = 1920.0f;
    static constexpr float kScreenH = 1080.0f;
    static constexpr float kMargin = 80.0f;
    /// 整体下移矩阵（像素，屏幕 Y 向下），为顶部缩圈外扩留出空间
    static constexpr float kMatrixShiftY = 70.0f;
    static constexpr float kDefaultCellW = 256.0f;
    static constexpr float kDefaultCellH = 256.0f;

    int32_t rows = 0;
    int32_t cols = 0;
    float blockSize = 1.0f;

    float gw() const { return kDefaultCellW; }
    float gh() const { return kDefaultCellH; }
    float contentW() const { return gw() * blockSize; }
    float contentH() const { return gh() * blockSize; }
    float matrixW() const { return static_cast<float>(std::max(cols, 0)) * gw(); }
    float matrixH() const { return static_cast<float>(std::max(rows, 0)) * gh(); }

    float activeWidth(int32_t activeStartCol, int32_t activeEndCol) const {
        return static_cast<float>(std::max(activeEndCol - activeStartCol + 1, 1));
    }

    float activeCenterOffset(int32_t activeStartCol, int32_t activeEndCol) const {
        return (activeWidth(activeStartCol, activeEndCol) - 1.0f) * 0.5f;
    }

    void activeBandX(int32_t activeStartCol, int32_t activeEndCol,
                     float& outLeft, float& outRight) const {
        const float width = activeWidth(activeStartCol, activeEndCol) * gw();
        outLeft = kScreenW * 0.5f - width * 0.5f;
        outRight = outLeft + width;
    }

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

    bool cellVisible(float cellX, float cellY) const {
        const float halfW = gw() * 0.5f;
        const float halfH = gh() * 0.5f;
        return cellX + halfW >= 0.0f && cellX - halfW <= kScreenW
            && cellY + halfH >= 0.0f && cellY - halfH <= kScreenH;
    }
};

} // namespace melody_matrix::renderer

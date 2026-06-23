#pragma once

#include "renderer/shader.h"
#include "beatmap/note.h"
#include "beatmap/beatmap.h"
#include "gameplay/judge_strategy.h"

#include <vector>
#include <cstdint>

namespace melody_matrix::renderer {

/// 音符渲染器 — 在网格上绘制点音符方块和长按音符条。
/// 使用实例化渲染提高效率。
class NoteRenderer {
public:
    NoteRenderer() = default;
    ~NoteRenderer() = default;

    bool init();
    void render(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                int rows, int cols, float ar,
                int32_t activeStartCol, int32_t activeEndCol,
                const std::array<size_t, 8>& colHeads, int32_t colHeadCount);
    void shutdown();

private:
    void buildNoteVertices(const std::vector<beatmap::Note>& notes, int64_t timeMs,
                           int rows, int cols, float ar,
                           int32_t activeStartCol, int32_t activeEndCol,
                           const std::array<size_t, 8>& colHeads, int32_t colHeadCount,
                           std::vector<float>& quads,
                           std::vector<float>& colors);

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_quadVbo = 0;    // 单位四边形（共享几何体）
    uint32_t m_instanceVbo = 0; // 实例数据（位置+大小）
    uint32_t m_colorVbo = 0;   // 实例颜色
    Shader m_shader;
    int32_t m_maxInstances = 256;
};

} // namespace melody_matrix::renderer

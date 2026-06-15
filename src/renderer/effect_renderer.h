#pragma once

#include "renderer/shader.h"
#include "gameplay/judge_strategy.h"
#include <cstdint>
#include <vector>

namespace melody_matrix::renderer {

/// 用于击中效果的简单粒子
struct Particle {
    float x, y;        // 位置
    float vx, vy;      // 速度
    float life;        // 剩余生命（0..1）
    float maxLife;     // 初始生命
    float size;        // 粒子大小
    float r, g, b, a;  // 颜色
};

/// 特效渲染器 — 绘制击中/连击/链粒子效果。
/// 粒子在判定事件时生成，并随时间淡出。
class EffectRenderer {
public:
    EffectRenderer() = default;
    ~EffectRenderer() = default;

    bool init();
    void update(float dtSeconds);
    void render();
    void shutdown();

    /// 在屏幕位置为判定结果生成粒子
    void spawnHitEffect(gameplay::JudgmentResult result, float x, float y);

    /// 生成连击里程碑效果
    void spawnComboEffect(int32_t combo, float x, float y);

private:
    void addParticle(const Particle& p);

    bool m_initialized = false;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    Shader m_shader;
    std::vector<Particle> m_particles;
    static constexpr size_t MAX_PARTICLES = 512;
};

} // namespace melody_matrix::renderer

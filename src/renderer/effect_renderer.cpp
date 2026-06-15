#include "effect_renderer.h"
#include "util/logger.h"

#include <glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <random>

namespace melody_matrix::renderer {

bool EffectRenderer::init() {
    MM_LOG_INFO("EffectRenderer", "Initializing...");

    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        layout(location = 2) in float aSize;

        uniform mat4 uProjection;
        out vec4 vColor;

        void main() {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
            gl_PointSize = aSize;
            vColor = aColor;
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            // Circular point shape
            vec2 coord = gl_PointCoord - vec2(0.5);
            float dist = length(coord);
            if (dist > 0.5) discard;
            float alpha = vColor.a * (1.0 - dist * 2.0);
            FragColor = vec4(vColor.rgb, alpha);
        }
    )";

    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        m_shader = std::move(result.value());
    } else {
        MM_LOG_WARN("EffectRenderer", "Shader failed: " + result.error().message);
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);

    // Color (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(2 * sizeof(float)));

    // Size (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);

    m_particles.reserve(MAX_PARTICLES);
    m_initialized = true;
    MM_LOG_INFO("EffectRenderer", "Initialized");
    return true;
}

void EffectRenderer::update(float dtSeconds) {
    for (auto& p : m_particles) {
        p.x += p.vx * dtSeconds;
        p.y += p.vy * dtSeconds;
        p.vy += 200.0f * dtSeconds; // Gravity
        p.life -= dtSeconds / p.maxLife;
        p.a = std::max(0.0f, p.life);
        p.size *= (1.0f - dtSeconds * 0.5f); // Shrink
    }

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end()
    );
}

void EffectRenderer::render() {
    if (!m_initialized || m_particles.empty()) return;

    // Build vertex data
    std::vector<float> data;
    data.reserve(m_particles.size() * 7);

    for (const auto& p : m_particles) {
        data.push_back(p.x);
        data.push_back(p.y);
        data.push_back(p.r);
        data.push_back(p.g);
        data.push_back(p.b);
        data.push_back(p.a);
        data.push_back(p.size);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);

    m_shader.use();
    glm::mat4 proj = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
    m_shader.setMat4("uProjection", &proj[0][0]);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_particles.size()));
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void EffectRenderer::spawnHitEffect(gameplay::JudgmentResult result, float x, float y) {
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.283f);
    std::uniform_real_distribution<float> speedDist(100.0f, 400.0f);

    int count = 0;
    float r = 0, g = 0, b = 0;

    switch (result) {
    case gameplay::JudgmentResult::Perfect:
        count = 20; r = 0.0f; g = 1.0f; b = 0.96f; // Cyan
        break;
    case gameplay::JudgmentResult::Good:
        count = 12; r = 0.702f; g = 0.0f; b = 1.0f; // Purple
        break;
    case gameplay::JudgmentResult::Miss:
        count = 8; r = 1.0f; g = 0.0f; b = 0.431f; // Pink
        break;
    default:
        return;
    }

    for (int i = 0; i < count; ++i) {
        float angle = angleDist(rng);
        float speed = speedDist(rng);
        Particle p;
        p.x = x; p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 100.0f;
        p.life = 1.0f;
        p.maxLife = 0.5f + 0.3f * (result == gameplay::JudgmentResult::Perfect ? 1.0f : 0.5f);
        p.size = 8.0f + 12.0f * (result == gameplay::JudgmentResult::Perfect ? 1.0f : 0.5f);
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        addParticle(p);
    }
}

void EffectRenderer::spawnComboEffect(int32_t combo, float x, float y) {
    // Big burst for combo milestones
    static std::mt19937 rng(123);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.283f);
    std::uniform_real_distribution<float> speedDist(200.0f, 600.0f);

    int count = 30;
    for (int i = 0; i < count; ++i) {
        float angle = angleDist(rng);
        float speed = speedDist(rng);
        Particle p;
        p.x = x; p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 150.0f;
        p.life = 1.0f;
        p.maxLife = 0.8f;
        p.size = 12.0f + 8.0f;
        // Rainbow based on angle
        p.r = 0.5f + 0.5f * std::cos(angle);
        p.g = 0.5f + 0.5f * std::cos(angle + 2.094f);
        p.b = 0.5f + 0.5f * std::cos(angle + 4.189f);
        p.a = 1.0f;
        addParticle(p);
    }
}

void EffectRenderer::addParticle(const Particle& p) {
    if (m_particles.size() >= MAX_PARTICLES) {
        m_particles.erase(m_particles.begin()); // Remove oldest
    }
    m_particles.push_back(p);
}

void EffectRenderer::shutdown() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_particles.clear();
    m_initialized = false;
}

} // namespace melody_matrix::renderer

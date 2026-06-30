// ============================================================
// shader.cpp — 着色器编译/链接与 uniform 设置
// ============================================================

#include "shader.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <glad.h>

namespace melody_matrix::renderer {

// ── RAII：析构/移动时释放 GL 程序 ──

Shader::~Shader() {
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
}

Shader::Shader(Shader&& other) noexcept : m_programId(other.m_programId) {
    other.m_programId = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_programId != 0) {
            glDeleteProgram(m_programId);
        }
        m_programId = other.m_programId;
        other.m_programId = 0;
    }
    return *this;
}

// ── 编译与链接 ──

util::Result<uint32_t> Shader::compileShader(uint32_t type, const std::string& source) {
    uint32_t shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int32_t success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        return util::failure<uint32_t>(
            static_cast<int32_t>(util::ErrorCode::ERROR_SHADER_COMPILE),
            std::string("Shader compile error: ") + log);
    }
    return shader;
}

util::Result<uint32_t> Shader::linkProgram(uint32_t vertShader, uint32_t fragShader) {
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    int32_t success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        return util::failure<uint32_t>(
            static_cast<int32_t>(util::ErrorCode::ERROR_SHADER_LINK),
            std::string("Shader link error: ") + log);
    }
    return program;
}

util::Result<Shader> Shader::compile(const std::string& vertexSource,
                                      const std::string& fragmentSource) {
    auto vertResult = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertResult.ok()) {
        return util::Result<Shader>(vertResult.error());
    }

    auto fragResult = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragResult.ok()) {
        glDeleteShader(vertResult.value());
        return util::Result<Shader>(fragResult.error());
    }

    auto programResult = linkProgram(vertResult.value(), fragResult.value());

    // 链接完成后删除独立 shader 对象（程序已持有副本）
    glDeleteShader(vertResult.value());
    glDeleteShader(fragResult.value());

    if (!programResult.ok()) {
        return util::Result<Shader>(programResult.error());
    }

    return Shader(programResult.value());
}

// ── 激活与 uniform 上传 ──

void Shader::use() const {
    if (m_programId != 0) {
        glUseProgram(m_programId);
    }
}

void Shader::setInt(const std::string& name, int32_t value) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniform1i(loc, value);
}

void Shader::setFloat(const std::string& name, float value) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniform1f(loc, value);
}

void Shader::setVec2(const std::string& name, float x, float y) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniform2f(loc, x, y);
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniform3f(loc, x, y, z);
}

void Shader::setVec4(const std::string& name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniform4f(loc, x, y, z, w);
}

void Shader::setMat4(const std::string& name, const float* value) const {
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, value);
}

// ── 后备纯色 shader（编译失败时的兜底）──

Shader FallbackShader::createFallback() {
    const std::string vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const std::string fragSrc = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

    auto result = Shader::compile(vertSrc, fragSrc);
    if (result.ok()) {
        return std::move(result.value());
    }
    // 连后备 shader 都失败则返回空程序
    MM_LOG_ERROR("Shader", "Fallback shader compilation failed!");
    return Shader(0);
}

Shader& FallbackShader::get() {
    static Shader s_fallback = createFallback();
    return s_fallback;
}

} // namespace melody_matrix::renderer

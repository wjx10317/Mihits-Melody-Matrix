#pragma once

#include "util/result.h"
#include <string>
#include <cstdint>

namespace melody_matrix::renderer {

/// OpenGL 着色器程序的 RAII 包装器。
/// 编译顶点 + 片段着色器，链接程序。
/// 失败时返回包含错误详情的 Result；永不抛出异常。
class Shader {
public:
    Shader() = default;
    ~Shader();

    // 仅可移动（RAII）
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /// 从源代码字符串编译并链接着色器程序。
    /// 成功时返回链接后的程序，或返回错误。
    static util::Result<Shader> compile(const std::string& vertexSource,
                                         const std::string& fragmentSource);

    /// 将此着色器程序用于后续绘制调用
    void use() const;

    /// 获取 OpenGL 程序 ID
    uint32_t programId() const { return m_programId; }

    /// Uniform 设置器
    void setInt(const std::string& name, int32_t value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec4(const std::string& name, float x, float y, float z, float w) const;
    void setMat4(const std::string& name, const float* value) const;

    /// 检查着色器是否有效（编译和链接成功）
    bool valid() const { return m_programId != 0; }

private:
    friend class FallbackShader;
    explicit Shader(uint32_t programId) : m_programId(programId) {}

    static util::Result<uint32_t> compileShader(uint32_t type, const std::string& source);
    static util::Result<uint32_t> linkProgram(uint32_t vertShader, uint32_t fragShader);

    uint32_t m_programId = 0;
};

/// 后备纯色着色器（当普通着色器编译失败时使用）
class FallbackShader {
public:
    /// 获取单例后备着色器。首次调用时创建。
    static Shader& get();

private:
    static Shader createFallback();
};

} // namespace melody_matrix::renderer

#pragma once

// ============================================================
// shader.h — OpenGL 着色器程序 RAII 包装
// 编译/链接顶点+片段 shader，提供 uniform 设置与后备纯色 shader。
// ============================================================

#include "util/result.h"  // Result<T> 错误传递
#include <string>         // 着色器源码与 uniform 名
#include <cstdint>        // uint32_t, int32_t

namespace melody_matrix::renderer {

/// OpenGL 着色器程序的 RAII 包装器。
/// 编译顶点 + 片段着色器，链接程序；失败时返回 Result 错误，不抛异常。
class Shader {
public:
    Shader() = default;   // 空程序（m_programId=0）
    ~Shader();            // 析构时 glDeleteProgram

    // 仅可移动（RAII）
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /// 从源代码字符串编译并链接着色器程序
    static util::Result<Shader> compile(const std::string& vertexSource,
                                         const std::string& fragmentSource);

    /// 激活此着色器程序（glUseProgram）
    void use() const;

    uint32_t programId() const { return m_programId; }  ///< GL 程序对象 ID

    /// Uniform 设置器（按名称查找 location，-1 则跳过）
    void setInt(const std::string& name, int32_t value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec4(const std::string& name, float x, float y, float z, float w) const;
    void setMat4(const std::string& name, const float* value) const;

    bool valid() const { return m_programId != 0; }  ///< 是否链接成功

private:
    friend class FallbackShader;
    explicit Shader(uint32_t programId) : m_programId(programId) {}  ///< 内部构造已链接程序

    static util::Result<uint32_t> compileShader(uint32_t type, const std::string& source);
    static util::Result<uint32_t> linkProgram(uint32_t vertShader, uint32_t fragShader);

    uint32_t m_programId = 0;  ///< 0 表示无效/未编译
};

/// 后备纯色着色器（主 shader 编译失败时使用）
class FallbackShader {
public:
    static Shader& get();  ///< 懒加载单例后备程序

private:
    static Shader createFallback();  ///< 编译最小 vert+frag
};

} // namespace melody_matrix::renderer

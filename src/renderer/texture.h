#pragma once

// ============================================================
// texture.h — OpenGL 2D 纹理 RAII 包装
// 通过 stb_image 加载图像，创建 GL 纹理；析构时自动 glDeleteTextures。
// ============================================================

#include <cstdint>
#include <string>

namespace melody_matrix::renderer {

/// OpenGL 2D 纹理的 RAII 包装器。
/// 通过 stb_image 加载图像文件，创建 GL 纹理，销毁时自动清理。
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    // 仅可移动（RAII）
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    /// 加载图像并上传为 GL_TEXTURE_2D（JPEG/PNG/BMP/TGA 等）
    /// @param genMipmap  是否生成 mipmap（note 纹理建议 true）
    bool loadFromFile(const std::string& path, bool genMipmap = true);

    /// 绑定到指定纹理单元（0 = GL_TEXTURE0）
    void bind(uint32_t unit = 0) const;

    /// 从指定单元解绑
    static void unbind(uint32_t unit = 0);

    uint32_t textureId() const { return m_textureId; }
    int32_t width() const { return m_width; }
    int32_t height() const { return m_height; }
    int32_t channels() const { return m_channels; }
    bool valid() const { return m_textureId != 0; }

private:
    void release();

    uint32_t m_textureId = 0;
    int32_t m_width = 0;
    int32_t m_height = 0;
    int32_t m_channels = 0;
};

} // namespace melody_matrix::renderer

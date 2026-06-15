#include "texture.h"
#include "util/logger.h"

#include <glad.h>
#include <stb_image.h>

namespace melody_matrix::renderer {

// ── RAII ──

Texture2D::~Texture2D() {
    release();
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : m_textureId(other.m_textureId)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_channels(other.m_channels) {
    other.m_textureId = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_channels = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        release();
        m_textureId = other.m_textureId;
        m_width = other.m_width;
        m_height = other.m_height;
        m_channels = other.m_channels;
        other.m_textureId = 0;
        other.m_width = 0;
        other.m_height = 0;
        other.m_channels = 0;
    }
    return *this;
}

void Texture2D::release() {
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_width = 0;
    m_height = 0;
    m_channels = 0;
}

// ── Loading ──

bool Texture2D::loadFromFile(const std::string& path, bool genMipmap) {
    // Clean up any existing texture
    release();

    // stb_image loads images with origin at top-left by default.
    // OpenGL expects origin at bottom-left, so flip vertically.
    stbi_set_flip_vertically_on_load(1);

    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        MM_LOG_ERROR("Texture2D", "Failed to load image: " + path + " — " + stbi_failure_reason());
        return false;
    }

    // Determine GL format based on number of channels
    GLenum internalFormat = 0;
    GLenum dataFormat = 0;
    if (channels == 4) {
        internalFormat = GL_RGBA8;
        dataFormat = GL_RGBA;
    } else if (channels == 3) {
        internalFormat = GL_RGB8;
        dataFormat = GL_RGB;
    } else {
        MM_LOG_ERROR("Texture2D", "Unsupported channel count " + std::to_string(channels) + " in: " + path);
        stbi_image_free(data);
        return false;
    }

    // ── 关键：设置像素解包对齐为 1 ──
    // OpenGL 默认 GL_UNPACK_ALIGNMENT=4，而 RGB 图片每像素 3 字节，
    // 当行宽不是 4 的倍数时 glTexImage2D 会越界读取导致崩溃。
    // 必须在 glTexImage2D 之前设置。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Create OpenGL texture
    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // Upload pixel data
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 dataFormat, GL_UNSIGNED_BYTE, data);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    genMipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Generate mipmaps if requested
    if (genMipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    // Free stb_image data
    stbi_image_free(data);

    m_width = width;
    m_height = height;
    m_channels = channels;

    MM_LOG_INFO("Texture2D", "Loaded texture: " + path +
                 " (" + std::to_string(width) + "x" + std::to_string(height) +
                 ", " + std::to_string(channels) + " channels)");

    return true;
}

// ── Binding ──

void Texture2D::bind(uint32_t unit) const {
    if (m_textureId == 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void Texture2D::unbind(uint32_t unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace melody_matrix::renderer

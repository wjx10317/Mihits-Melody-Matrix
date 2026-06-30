// ============================================================
// texture.cpp — 2D 纹理加载与绑定
// stb_image 垂直翻转以匹配 OpenGL 纹理坐标原点（左下）。
// ============================================================

#include "texture.h"       // Texture2D 声明
#include "util/logger.h"   // MM_LOG_* 日志宏

#include <glad.h>          // OpenGL 函数指针
#include <stb_image.h>     // stbi_load / stbi_image_free

namespace melody_matrix::renderer {

// ── RAII：析构/移动时 glDeleteTextures ──

Texture2D::~Texture2D() {
    release();  // 析构时释放 GPU 纹理
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : m_textureId(other.m_textureId)      // 接管对方纹理 ID
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_channels(other.m_channels) {
    other.m_textureId = 0;   // 源对象置空，避免双重释放
    other.m_width = 0;
    other.m_height = 0;
    other.m_channels = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {           // 自赋值保护
        release();                  // 先释放自身已有纹理
        m_textureId = other.m_textureId;
        m_width = other.m_width;
        m_height = other.m_height;
        m_channels = other.m_channels;
        other.m_textureId = 0;      // 清空源对象
        other.m_width = 0;
        other.m_height = 0;
        other.m_channels = 0;
    }
    return *this;
}

void Texture2D::release() {
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);  // 删除 GL 纹理对象
        m_textureId = 0;
    }
    m_width = 0;      // 重置元数据
    m_height = 0;
    m_channels = 0;
}

// ── 从文件加载并上传 GPU ──

bool Texture2D::uploadFromMemory(const unsigned char* data, int width, int height, int channels,
                                 bool genMipmap, const std::string& debugLabel) {
    release();

    if (!data || width <= 0 || height <= 0) {
        return false;
    }

    GLenum internalFormat = 0;
    GLenum dataFormat = 0;
    if (channels == 4) {
        internalFormat = GL_RGBA8;
        dataFormat = GL_RGBA;
    } else if (channels == 3) {
        internalFormat = GL_RGB8;
        dataFormat = GL_RGB;
    } else {
        MM_LOG_ERROR("Texture2D", "Unsupported channel count " + std::to_string(channels) +
                     (debugLabel.empty() ? "" : " in: " + debugLabel));
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 dataFormat, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    genMipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (genMipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    m_width = width;
    m_height = height;
    m_channels = channels;

    if (!debugLabel.empty()) {
        MM_LOG_INFO("Texture2D", "Uploaded texture: " + debugLabel +
                     " (" + std::to_string(width) + "x" + std::to_string(height) +
                     ", " + std::to_string(channels) + " channels)");
    }

    return true;
}

bool Texture2D::loadFromFile(const std::string& path, bool genMipmap) {
    stbi_set_flip_vertically_on_load(1);

    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        MM_LOG_ERROR("Texture2D", "Failed to load image: " + path + " — " + stbi_failure_reason());
        return false;
    }

    const bool ok = uploadFromMemory(data, width, height, channels, genMipmap, path);
    stbi_image_free(data);
    return ok;
}

// ── 纹理单元绑定/解绑 ──

void Texture2D::bind(uint32_t unit) const {
    if (m_textureId == 0) return;                    // 无效纹理不绑定
    glActiveTexture(GL_TEXTURE0 + unit);             // 激活纹理单元
    glBindTexture(GL_TEXTURE_2D, m_textureId);       // 绑定本纹理到该单元
}

void Texture2D::unbind(uint32_t unit) {
    glActiveTexture(GL_TEXTURE0 + unit);             // 激活指定单元
    glBindTexture(GL_TEXTURE_2D, 0);                 // 绑定 0 表示解绑
}

} // namespace melody_matrix::renderer

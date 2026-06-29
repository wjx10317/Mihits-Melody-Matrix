#pragma once

#include "util/result.h"
#include <string>
#include <unordered_map>

namespace melody_matrix::platform {

/// 简单的 INI 风格配置管理器。
/// 延迟加载，更改时保存。
class Config {
public:
    /// 从文件加载配置（如果缺失则创建默认配置）
    static util::Result<void> load(const std::string& path = "config.ini");

    /// 将当前配置保存到文件
    static util::Result<void> save();

    // ── 获取器 ──
    static int32_t getInt(const std::string& key, int32_t defaultValue = 0);
    static float getFloat(const std::string& key, float defaultValue = 0.0f);
    static std::string getString(const std::string& key, const std::string& defaultValue = "");

    // ── 设置器 ──
    static void setInt(const std::string& key, int32_t value);
    static void setFloat(const std::string& key, float value);
    static void setString(const std::string& key, const std::string& value);

    // ── 已知配置键 ──
    static constexpr const char* KEY_VOLUME_MASTER = "audio.master_volume";
    static constexpr const char* KEY_VOLUME_MUSIC  = "audio.music_volume";
    static constexpr const char* KEY_VOLUME_SFX    = "audio.sfx_volume";
    static constexpr const char* KEY_OFFSET        = "gameplay.offset_ms";
    static constexpr const char* KEY_TIMING_OFFSET = "gameplay.timing_offset_ms";
    static constexpr const char* KEY_VISUAL_LEAD   = "gameplay.visual_lead_ms";
    static constexpr const char* KEY_DEBUG_HUD   = "gameplay.debug_hud";
    static constexpr const char* KEY_RESOLUTION_W  = "video.width";
    static constexpr const char* KEY_RESOLUTION_H  = "video.height";
    static constexpr const char* KEY_FULLSCREEN    = "video.fullscreen";
    static constexpr const char* KEY_EFFECTS_LEVEL = "video.effects_level";
    static constexpr const char* KEY_BG_DIM        = "video.bg_dim";
    static constexpr const char* KEY_OFFSET_BAR    = "gameplay.offset_bar";

private:
    static inline std::unordered_map<std::string, std::string> s_data;
    static inline std::string s_path;
    static inline bool s_loaded = false;
};

} // namespace melody_matrix::platform

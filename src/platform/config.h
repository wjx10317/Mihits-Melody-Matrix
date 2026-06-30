/**
 * @file config.h
 * @brief INI 风格配置管理器
 *
 * 文件职责：
 *   声明键值对配置的加载、保存及类型化读写 API，以及已知配置键常量。
 *
 * 主要依赖：
 *   util/result.h、标准库 <unordered_map>。
 *
 * 在项目中的用法：
 *   启动时 Config::load()；运行时 Config::getInt/getFloat/getString 读取；
 *   设置变更后 Config::set* + save()。Kernel 在 init/shutdown 时读写分辨率等。
 */
#pragma once

#include "util/result.h"
#include <string>
#include <unordered_map>

namespace melody_matrix::platform {

/**
 * @brief 简单的 INI 风格配置管理器
 *
 * 内存中维护 key=value 映射，延迟加载，修改后需显式 save()。
 * 所有方法均为静态，全局单份配置数据。
 */
class Config {
public:
    /**
     * @brief 从文件加载配置
     * @param path 配置文件路径，默认 "config.ini"
     * @return 成功 Result<void>；文件不存在时使用默认值仍返回成功
     */
    static util::Result<void> load(const std::string& path = "config.ini");

    /**
     * @brief 将当前配置写入文件
     * @return 成功或 IO 错误 Result
     */
    static util::Result<void> save();

    /**
     * @brief 读取整型配置
     * @param key 键名
     * @param defaultValue 键不存在或解析失败时的默认值
     * @return 整数值
     */
    static int32_t getInt(const std::string& key, int32_t defaultValue = 0);

    /**
     * @brief 读取浮点配置
     * @param key 键名
     * @param defaultValue 默认值
     * @return 浮点值
     */
    static float getFloat(const std::string& key, float defaultValue = 0.0f);

    /**
     * @brief 读取字符串配置
     * @param key 键名
     * @param defaultValue 默认值
     * @return 字符串值
     */
    static std::string getString(const std::string& key, const std::string& defaultValue = "");

    /**
     * @brief 设置整型配置（内存中，需 save 才持久化）
     * @param key 键名
     * @param value 值
     */
    static void setInt(const std::string& key, int32_t value);

    /**
     * @brief 设置浮点配置
     * @param key 键名
     * @param value 值
     */
    static void setFloat(const std::string& key, float value);

    /**
     * @brief 设置字符串配置
     * @param key 键名
     * @param value 值
     */
    static void setString(const std::string& key, const std::string& value);

    // ── 已知配置键常量 ──
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

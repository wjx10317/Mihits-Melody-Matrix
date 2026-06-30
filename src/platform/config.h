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
#pragma once  // 防止头文件重复包含

#include "util/result.h"           // Result<T> 成功/失败封装
#include <string>                  // 配置键值字符串
#include <unordered_map>           // 内存中 key→value 映射

namespace melody_matrix::platform {  // 平台层命名空间

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
    static util::Result<void> load(const std::string& path = "config.ini");  // 从磁盘加载

    /**
     * @brief 将当前配置写入文件
     * @return 成功或 IO 错误 Result
     */
    static util::Result<void> save();  // 持久化到磁盘

    /**
     * @brief 读取整型配置
     * @param key 键名
     * @param defaultValue 键不存在或解析失败时的默认值
     * @return 整数值
     */
    static int32_t getInt(const std::string& key, int32_t defaultValue = 0);  // 读取 int32

    /**
     * @brief 读取浮点配置
     * @param key 键名
     * @param defaultValue 默认值
     * @return 浮点值
     */
    static float getFloat(const std::string& key, float defaultValue = 0.0f);  // 读取 float

    /**
     * @brief 读取字符串配置
     * @param key 键名
     * @param defaultValue 默认值
     * @return 字符串值
     */
    static std::string getString(const std::string& key, const std::string& defaultValue = "");  // 读取 string

    /**
     * @brief 设置整型配置（内存中，需 save 才持久化）
     * @param key 键名
     * @param value 值
     */
    static void setInt(const std::string& key, int32_t value);  // 写入 int32

    /**
     * @brief 设置浮点配置
     * @param key 键名
     * @param value 值
     */
    static void setFloat(const std::string& key, float value);  // 写入 float

    /**
     * @brief 设置字符串配置
     * @param key 键名
     * @param value 值
     */
    static void setString(const std::string& key, const std::string& value);  // 写入 string

    // ── 已知配置键常量 ──
    static constexpr const char* KEY_VOLUME_MASTER = "audio.master_volume";       // 主音量
    static constexpr const char* KEY_VOLUME_MUSIC  = "audio.music_volume";        // 音乐音量
    static constexpr const char* KEY_VOLUME_SFX    = "audio.sfx_volume";          // 音效音量
    static constexpr const char* KEY_OFFSET        = "gameplay.offset_ms";        // 全局偏移（毫秒）
    static constexpr const char* KEY_TIMING_OFFSET = "gameplay.timing_offset_ms"; // 判定偏移
    static constexpr const char* KEY_VISUAL_LEAD   = "gameplay.visual_lead_ms";  // 视觉提前量
    static constexpr const char* KEY_DEBUG_HUD   = "gameplay.debug_hud";          // 调试 HUD 开关
    static constexpr const char* KEY_RESOLUTION_W  = "video.width";               // 窗口宽度
    static constexpr const char* KEY_RESOLUTION_H  = "video.height";              // 窗口高度
    static constexpr const char* KEY_FULLSCREEN    = "video.fullscreen";          // 全屏开关
    static constexpr const char* KEY_EFFECTS_LEVEL = "video.effects_level";       // 特效等级
    static constexpr const char* KEY_BG_DIM        = "video.bg_dim";              // 背景暗化
    static constexpr const char* KEY_OFFSET_BAR    = "gameplay.offset_bar";       // 偏移条显示

private:
    static inline std::unordered_map<std::string, std::string> s_data;  // 内存配置表
    static inline std::string s_path;                                   // 当前配置文件路径
    static inline bool s_loaded = false;                                // 是否已加载
};

} // namespace melody_matrix::platform

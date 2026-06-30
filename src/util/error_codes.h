/**
 * @file error_codes.h
 * @brief 应用程序统一错误码定义
 *
 * 文件职责：
 *   定义 ErrorCode 枚举及 errorCodeToString() 中文描述映射，供 Result<T> 和日志使用。
 *
 * 主要依赖：
 *   标准库 <string>、<cstdint>；无其他项目模块依赖。
 *
 * 在项目中的用法：
 *   各子系统返回 util::failure<T>(static_cast<int32_t>(ErrorCode::XXX), "...")；
 *   日志或 UI 可用 errorCodeToString() 展示用户可读信息。
 */
#pragma once  // 防止头文件重复包含

#include <string>    // errorCodeToString 返回类型（预留扩展）
#include <cstdint>   // int32_t 底层类型

namespace melody_matrix::util {  // 工具层命名空间

/**
 * @brief 应用程序中使用的错误码
 *
 * 按功能域分段编号：通用 1xxx、音频 2xxx、渲染 3xxx、谱面 4xxx、玩法 5xxx。
 */
enum class ErrorCode : int32_t {
    OK = 0,  // 成功，无错误

    // 通用 (1000-1999)
    ERROR_UNKNOWN = 1000,        // 未知错误
    ERROR_IO = 1001,             // 通用 I/O 错误
    ERROR_CONFIG_PARSE = 1002,   // 配置文件解析失败
    ERROR_FILE_NOT_FOUND = 1003, // 文件不存在
    ERROR_PATH_TRAVERSAL = 1004, // 检测到路径遍历攻击

    // 音频 (2000-2999)
    ERROR_AUDIO_INIT = 2000,     // 音频引擎初始化失败
    ERROR_AUDIO_DEVICE = 2001,     // 音频设备不可用或出错
    ERROR_AUDIO_DECODE = 2002,     // 音频解码失败

    // 渲染器 (3000-3999)
    ERROR_GL_CONTEXT = 3000,     // OpenGL 上下文创建失败
    ERROR_SHADER_COMPILE = 3001, // 着色器编译失败
    ERROR_SHADER_LINK = 3002,    // 着色器程序链接失败
    ERROR_TEXTURE_LOAD = 3003,   // 纹理加载失败

    // 谱面 (4000-4999)
    ERROR_BEATMAP_VERSION = 4000,          // 谱面格式版本不匹配
    ERROR_BEATMAP_AUDIO_MISSING = 4001,    // 谱面引用的音频文件缺失
    ERROR_BEATMAP_NOTE_OOB = 4002,         // 音符坐标超出阵型边界
    ERROR_BEATMAP_FORMATION = 4003,        // 阵型参数无效
    ERROR_BEATMAP_TIME_ORDER = 4004,       // 谱面时间顺序违反单调性
    ERROR_BEATMAP_MISSING_SECTION = 4005,  // 谱面缺少必需段落
    ERROR_BEATMAP_PARSE = 4006,            // 谱面解析失败
    ERROR_BEATMAP_EMPTY_NOTES = 4007,      // 谱面无音符数据
    ERROR_BEATMAP_NO_AUDIO_REF = 4008,     // 谱面未指定音频文件
    ERROR_BEATMAP_NO_TITLE = 4009,         // 谱面标题为空
    ERROR_BEATMAP_NOT_STANDARD = 4010,     // 非 osu!standard 模式
    ERROR_BEATMAP_ALREADY_IMPORTED = 4011, // 谱面已导入，重复导入
    ERROR_BEATMAP_INVALID_HEADER = 4012,   // 谱面格式头无效
    ERROR_BEATMAP_INVALID_ARCHIVE = 4013,  ///< .osz 解压失败
    ERROR_BEATMAP_NO_OSU_IN_ARCHIVE = 4014,///< .osz 中未找到 .osu 文件

    // 游戏玩法 (5000-5999)
    ERROR_GAMEPLAY_STATE = 5000,  // 游戏状态机错误
};

/**
 * @brief 将 ErrorCode 转换为中文描述字符串
 * @param code 错误码
 * @return 静态字符串，未知码返回 "未知错误码"
 */
inline const char* errorCodeToString(ErrorCode code) {
    switch (code) {  // 按错误码分支返回中文描述
    case ErrorCode::OK:                        return "成功";  // 无错误
    case ErrorCode::ERROR_UNKNOWN:             return "未知错误";  // 未分类错误
    case ErrorCode::ERROR_IO:                  return "I/O 错误";  // 通用 I/O 失败
    case ErrorCode::ERROR_CONFIG_PARSE:        return "配置解析错误";  // 配置解析失败
    case ErrorCode::ERROR_FILE_NOT_FOUND:      return "文件未找到";  // 文件不存在
    case ErrorCode::ERROR_PATH_TRAVERSAL:      return "检测到路径遍历";  // 路径遍历攻击
    case ErrorCode::ERROR_AUDIO_INIT:          return "音频初始化失败";  // 音频引擎初始化
    case ErrorCode::ERROR_AUDIO_DEVICE:        return "音频设备错误";  // 音频设备问题
    case ErrorCode::ERROR_AUDIO_DECODE:        return "音频解码错误";  // 音频解码失败
    case ErrorCode::ERROR_GL_CONTEXT:          return "GL 上下文创建失败";  // OpenGL 上下文
    case ErrorCode::ERROR_SHADER_COMPILE:      return "着色器编译错误";  // 着色器编译
    case ErrorCode::ERROR_SHADER_LINK:         return "着色器链接错误";  // 着色器链接
    case ErrorCode::ERROR_TEXTURE_LOAD:        return "纹理加载错误";  // 纹理加载
    case ErrorCode::ERROR_BEATMAP_VERSION:     return "谱面版本不匹配";  // 格式版本
    case ErrorCode::ERROR_BEATMAP_AUDIO_MISSING: return "谱面音频缺失";  // 音频文件缺失
    case ErrorCode::ERROR_BEATMAP_NOTE_OOB:    return "谱面音符越界";  // 音符坐标越界
    case ErrorCode::ERROR_BEATMAP_FORMATION:   return "谱面阵型无效";  // 阵型参数
    case ErrorCode::ERROR_BEATMAP_TIME_ORDER:  return "谱面时间顺序违反";  // 时间单调性
    case ErrorCode::ERROR_BEATMAP_MISSING_SECTION: return "谱面段落缺失";  // 缺少段落
    case ErrorCode::ERROR_BEATMAP_PARSE:       return "谱面解析错误";  // 解析失败
    case ErrorCode::ERROR_BEATMAP_EMPTY_NOTES: return "谱面无音符数据";  // 无音符
    case ErrorCode::ERROR_BEATMAP_NO_AUDIO_REF:return "谱面未指定音频文件";  // 无音频引用
    case ErrorCode::ERROR_BEATMAP_NO_TITLE:    return "谱面标题为空";  // 标题为空
    case ErrorCode::ERROR_BEATMAP_NOT_STANDARD:return "非 osu!standard 模式";  // 非标准模式
    case ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED: return "谱面已导入";  // 重复导入
    case ErrorCode::ERROR_BEATMAP_INVALID_HEADER: return "谱面格式头无效";  // 格式头无效
    case ErrorCode::ERROR_BEATMAP_INVALID_ARCHIVE: return "谱面压缩包解压失败";  // .osz 解压
    case ErrorCode::ERROR_BEATMAP_NO_OSU_IN_ARCHIVE: return "压缩包中未找到 .osu 文件";  // 无 .osu
    case ErrorCode::ERROR_GAMEPLAY_STATE:      return "游戏状态错误";  // 状态机错误
    default:                                  return "未知错误码";  // 未定义的错误码
    }
}

} // namespace melody_matrix::util

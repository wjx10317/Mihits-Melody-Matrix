/**
 * @file host_clock.h
 * @brief 跨平台主机单调时钟（HostClock）
 *
 * 提供 nowNs() / nowMs()，供 core::Clock 外推与按键时间域统一使用。
 * Windows 实现为 QPC；其它平台为 steady_clock 占位。
 * 头文件禁止包含平台 SDK（如 windows.h）。
 */
#pragma once

#include <cstdint>

namespace melody_matrix::time {

/**
 * @brief 主机单调高精度时钟（与歌曲 Clock 职责分离）
 *
 * 不表示歌曲时间；只表示「主机上的单调时刻」。
 * Win = QueryPerformanceCounter；其它平台 = steady_clock stub。
 */
class HostClock {
public:
    HostClock() = delete;

    /// 单调纳秒（自进程内固定 epoch 起，不保证与墙钟对齐）
    static int64_t nowNs();

    /// 单调毫秒（由 nowNs 截断，与 nowNs 同 epoch）
    static int64_t nowMs();
};

} // namespace melody_matrix::time

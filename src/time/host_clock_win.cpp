/**
 * @file host_clock_win.cpp
 * @brief HostClock Windows 实现（QueryPerformanceCounter）
 */
#ifdef _WIN32

#include "time/host_clock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace melody_matrix::time {
namespace {

LARGE_INTEGER qpcFrequency() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return freq;
}

} // namespace

int64_t HostClock::nowNs() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const int64_t ticks = counter.QuadPart;
    const int64_t freq = qpcFrequency().QuadPart;
    // 先乘后除，避免过早截断；freq 通常为 10M～几十 M
    return (ticks * 1000000000LL) / freq;
}

int64_t HostClock::nowMs() {
    return nowNs() / 1000000LL;
}

} // namespace melody_matrix::time

#endif // _WIN32

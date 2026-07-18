/**
 * @file host_clock_stub.cpp
 * @brief HostClock 非 Windows 占位（steady_clock）
 *
 * 保证其它平台可链接；精度与 Windows QPC 不对等，后续可换平台原生钟。
 */
#ifndef _WIN32

#include "time/host_clock.h"

#include <chrono>

namespace melody_matrix::time {

int64_t HostClock::nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

int64_t HostClock::nowMs() {
    return nowNs() / 1000000LL;
}

} // namespace melody_matrix::time

#endif // !_WIN32

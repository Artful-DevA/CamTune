// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>

namespace cam {

// CLOCK_MONOTONIC in nanoseconds; the same clock V4L2 uses for buffer timestamps.
inline int64_t monotonicNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

} // namespace cam

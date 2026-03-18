#include "core/time_utils.h"

#include <time.h>

namespace ros2_android {
namespace time_utils {

int64_t GetBootTimeOffsetNs() {
  struct timespec realtime_ts, boottime_ts;

  // Sample both clocks as close together as possible to minimize drift
  if (clock_gettime(CLOCK_REALTIME, &realtime_ts) == 0 &&
      clock_gettime(CLOCK_BOOTTIME, &boottime_ts) == 0) {

    int64_t realtime_ns = static_cast<int64_t>(realtime_ts.tv_sec) * 1000000000LL +
                          realtime_ts.tv_nsec;
    int64_t boottime_ns = static_cast<int64_t>(boottime_ts.tv_sec) * 1000000000LL +
                          boottime_ts.tv_nsec;

    return realtime_ns - boottime_ns;
  }

  // Fallback if clock_gettime fails (unlikely on Android, but safe to handle)
  return 0;
}

}  // namespace time_utils
}  // namespace ros2_android

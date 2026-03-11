#include <android/log.h>

#include <rcl_logging_interface/rcl_logging_interface.h>
#include <rcutils/allocator.h>
#include <rcutils/logging.h>

static const char *DEFAULT_TAG = "ROS2";

static inline android_LogPriority map_severity_to_android_priority(int severity)
{
  switch (severity)
  {
  case RCUTILS_LOG_SEVERITY_DEBUG:
    return ANDROID_LOG_DEBUG;
  case RCUTILS_LOG_SEVERITY_INFO:
    return ANDROID_LOG_INFO;
  case RCUTILS_LOG_SEVERITY_WARN:
    return ANDROID_LOG_WARN;
  case RCUTILS_LOG_SEVERITY_ERROR:
    return ANDROID_LOG_ERROR;
  case RCUTILS_LOG_SEVERITY_FATAL:
    return ANDROID_LOG_FATAL;
  default:
    return (severity <= RCUTILS_LOG_SEVERITY_DEBUG) ? ANDROID_LOG_VERBOSE
                                                    : ANDROID_LOG_FATAL;
  }
}

rcl_logging_ret_t rcl_logging_external_initialize(
    const char *config, rcutils_allocator_t allocator)
{
  (void)config;
  (void)allocator;
  return RCL_LOGGING_RET_OK;
}

rcl_logging_ret_t rcl_logging_external_shutdown(void)
{
  return RCL_LOGGING_RET_OK;
}

void rcl_logging_external_log(int severity, const char *name, const char *msg)
{
  if (msg == NULL)
  {
    return;
  }

  const char *tag = (name != NULL && name[0] != '\0') ? name : DEFAULT_TAG;
  android_LogPriority priority = map_severity_to_android_priority(severity);

  __android_log_write(priority, tag, msg);
}

rcl_logging_ret_t rcl_logging_external_set_logger_level(
    const char *name, int level)
{
  (void)name;
  (void)level;
  // Android logcat filtering is done system-wide via adb logcat, not per-logger
  // Per-logger level control is handled by rcutils internally
  return RCL_LOGGING_RET_OK;
}

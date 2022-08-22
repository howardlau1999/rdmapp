#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>

#ifndef SOURCE_PATH_LENGTH
#define SOURCE_PATH_LENGTH 0
#endif

#define __RDMAPP_FILENAME__ (&__FILE__[SOURCE_PATH_LENGTH])

namespace rdmapp {

enum class LogLevel {
  TRACE,
  DEBUG,
  INFO,
  WARN,
  ERROR,
};

}

constexpr static inline rdmapp::LogLevel rdmapp_log_level =
    rdmapp::LogLevel::DEBUG;

#define RDMAPP_LOG_TRACE(msg, ...)                                             \
  do {                                                                         \
    if (rdmapp_log_level > rdmapp::LogLevel::TRACE)                            \
      break;                                                                   \
    printf("[TRACE] [%s:%d] " msg "\n", __RDMAPP_FILENAME__,                   \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_DEBUG(msg, ...)                                             \
  do {                                                                         \
    if (rdmapp_log_level > rdmapp::LogLevel::DEBUG)                            \
      break;                                                                   \
    printf("[DEBUG] [%s:%d] " msg "\n", __RDMAPP_FILENAME__,                   \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_INFO(msg, ...)                                              \
  do {                                                                         \
    if (rdmapp_log_level > rdmapp::LogLevel::INFO)                             \
      break;                                                                   \
    printf("[INFO ] [%s:%d] " msg "\n", __RDMAPP_FILENAME__,                   \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_ERROR(msg, ...)                                             \
  do {                                                                         \
    printf("[ERROR] [%s:%d] " msg "\n", __RDMAPP_FILENAME__,                   \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

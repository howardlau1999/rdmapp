#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>

#ifndef SOURCE_PATH_LENGTH
#define SOURCE_PATH_LENGTH 0
#endif

#define __FILENAME__ (&__FILE__[SOURCE_PATH_LENGTH])

enum class LogLevel {
  TRACE,
  DEBUG,
  INFO,
  WARN,
  ERROR,
};

constexpr static inline LogLevel log_level = LogLevel::DEBUG;

#define RDMAPP_LOG_TRACE(msg, ...)                                             \
  do {                                                                         \
    if (log_level > LogLevel::TRACE)                                           \
      break;                                                                   \
    printf("[TRACE] [%s:%d] " msg "\n", __FILENAME__,                          \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_DEBUG(msg, ...)                                             \
  do {                                                                         \
    if (log_level > LogLevel::DEBUG)                                           \
      break;                                                                   \
    printf("[DEBUG] [%s:%d] " msg "\n", __FILENAME__,                          \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_INFO(msg, ...)                                              \
  do {                                                                         \
    if (log_level > LogLevel::INFO)                                            \
      break;                                                                   \
    printf("[INFO ] [%s:%d] " msg "\n", __FILENAME__,                          \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

#define RDMAPP_LOG_ERROR(msg, ...)                                             \
  do {                                                                         \
    printf("[ERROR] [%s:%d] " msg "\n", __FILENAME__,                          \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);                               \
  } while (0)

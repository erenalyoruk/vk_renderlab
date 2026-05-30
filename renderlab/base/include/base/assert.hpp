#pragma once

#include <cstdlib>  // IWYU pragma: keep

#include "base/log.hpp"

#if defined(__clang__) || defined(__GNUC__)
  #define RL_DEBUG_BREAK() __builtin_trap()
#else
  #define RL_DEBUG_BREAK() std::abort()
#endif

#ifndef NDEBUG
  #define RL_ASSERT(condition, ...)                           \
    do {                                                      \
      if (!(condition)) {                                     \
        RL_CORE_CRITICAL("assertion failed: {}", #condition); \
        __VA_OPT__(RL_CORE_CRITICAL(__VA_ARGS__);)            \
        RL_DEBUG_BREAK();                                     \
      }                                                       \
    } while (false)
#else
  #define RL_ASSERT(condition, ...) \
    do {                            \
      (void)sizeof(condition);      \
    } while (false)
#endif

#ifndef NDEBUG
  #define RL_VERIFY(condition, ...) RL_ASSERT(condition, __VA_ARGS__)
#else
  #define RL_VERIFY(condition, ...)                           \
    do {                                                      \
      if (!(condition)) {                                     \
        RL_CORE_ERROR("verification failed: {}", #condition); \
        __VA_OPT__(RL_CORE_ERROR(__VA_ARGS__);)               \
      }                                                       \
    } while (false)
#endif

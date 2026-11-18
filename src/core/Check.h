#pragma once
// (File is Check.h because Assert.h would shadow <assert.h> on a case-insensitive filesystem.)
#include "Log.h"

// VRTC_CHECK is always on: use it for start-up and per-chunk conditions.
// VRTC_ASSERT compiles out in release builds: use it where the check would sit in a hot loop.
#define VRTC_CHECK(cond, ...)                                                                        \
    do {                                                                                             \
        if (!(cond)) ::vrtc::log::Fatal("check failed: {} at {}:{}: {}", #cond, __FILE__, __LINE__,   \
                                         ::std::format(__VA_ARGS__));                                \
    } while (0)

#ifdef NDEBUG
#define VRTC_ASSERT(cond, ...) ((void) 0)
#else
#define VRTC_ASSERT(cond, ...) VRTC_CHECK(cond, __VA_ARGS__)
#endif

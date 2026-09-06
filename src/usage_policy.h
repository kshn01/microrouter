#pragma once

#include <stdint.h>

inline uint64_t byteCounterDelta(uint64_t currentBytes, uint64_t previousBytes) {
    return currentBytes >= previousBytes ? currentBytes - previousBytes : currentBytes;
}
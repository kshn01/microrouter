#pragma once

#include <stdint.h>
#include <string.h>

inline bool isScheduleActive(bool enabled, bool timeSynced,
                             int currentMinute, int startMinute, int endMinute) {
    if (!enabled || !timeSynced) return false;

    if (startMinute <= endMinute) {
        return currentMinute >= startMinute && currentMinute < endMinute;
    }

    return currentMinute >= startMinute || currentMinute < endMinute;
}

inline bool canGrantWaiver(uint8_t waiversGrantedToday, uint8_t maxWaiversPerDay) {
    return waiversGrantedToday < maxWaiversPerDay;
}
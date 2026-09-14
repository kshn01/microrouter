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

inline bool canGrantWaiverWithoutPin(uint8_t waiversGrantedToday, uint8_t maxWaiversPerDay) {
    return waiversGrantedToday < maxWaiversPerDay;
}

inline bool verifyWaiverPin(const char* inputPin, const char* configuredPin) {
    if (!configuredPin || configuredPin[0] == '\0') return false;
    if (!inputPin) return false;
    return strcmp(inputPin, configuredPin) == 0;
}
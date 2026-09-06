#pragma once

inline bool isScheduleActive(bool enabled, bool timeSynced,
                             int currentMinute, int startMinute, int endMinute) {
    if (!enabled || !timeSynced) return false;

    if (startMinute <= endMinute) {
        return currentMinute >= startMinute && currentMinute < endMinute;
    }

    return currentMinute >= startMinute || currentMinute < endMinute;
}
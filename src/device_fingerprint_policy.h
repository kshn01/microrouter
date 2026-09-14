#pragma once

#include <cstdint>
#include <cstring>
#include <cctype>

inline bool isSyntheticMac(const char* mac) {
    if (!mac) return false;
    return (strncmp(mac, "02:00:", 6) == 0);
}

inline bool isRandomizedMac(const char* mac) {
    if (!mac || strlen(mac) < 2) return false;
    if (isSyntheticMac(mac)) return false;

    // IEEE 802 Locally Administered Address (LAA) bit is bit 1 of the first octet.
    // In standard colon/hyphen hex notation ("XX:..."), mac[1] is the low nibble of octet 0.
    // Values with bit 1 set: 2, 3, 6, 7, A, B, E, F (case-insensitive).
    char c = mac[1];
    return (c == '2' || c == '3' || c == '6' || c == '7' ||
            c == 'a' || c == 'b' || c == 'e' || c == 'f' ||
            c == 'A' || c == 'B' || c == 'E' || c == 'F');
}

inline bool isGenericHostname(const char* hostname) {
    if (!hostname) return true;
    while (*hostname == ' ' || *hostname == '\t' || *hostname == '\r' || *hostname == '\n') {
        hostname++;
    }
    size_t len = strlen(hostname);
    if (len < 2) return true;

    // Reject generic/default placeholder names (case-insensitive)
    static const char* const GENERIC_NAMES[] = {
        "unknown",
        "localhost",
        "*",
        "client",
        "device",
        "esp32",
        "esp8266",
        "esp32s2",
        "esp32s3",
        "esp32c3",
        "wlan",
        "wireless",
        "router",
        "gateway",
        "broadcom",
        "android",
        "iphone",
        "ipad",
        "ipod",
        "macbook",
        "pc",
        "computer",
        "windows",
        "linux",
        "generic",
        "new-device"
    };

    for (size_t i = 0; i < sizeof(GENERIC_NAMES) / sizeof(GENERIC_NAMES[0]); i++) {
        if (strcasecmp(hostname, GENERIC_NAMES[i]) == 0) {
            return true;
        }
    }
    return false;
}

inline bool canMergeDeviceProfiles(const char* macA, const char* hostA,
                                  const char* macB, const char* hostB) {
    if (!macA || !macB || !hostA || !hostB) return false;
    if (strcasecmp(macA, macB) == 0) return false;
    if (isSyntheticMac(macA) || isSyntheticMac(macB)) return false;

    if (isGenericHostname(hostA) || isGenericHostname(hostB)) return false;
    if (strcasecmp(hostA, hostB) != 0) return false;

    // Merge only if at least one of the devices is using a randomized MAC address.
    // Two distinct burned-in factory hardware MACs must never be merged.
    return (isRandomizedMac(macA) || isRandomizedMac(macB));
}

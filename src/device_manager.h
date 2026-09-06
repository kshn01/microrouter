#pragma once

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  DeviceManager — Active Clients, NetBIOS & Access Control   ║
// ╚══════════════════════════════════════════════════════════════╝

#define MAX_TRACKED_DEVICES 64

struct ClientDevice {
    char     mac[18];
    char     ip[16];
    char     hostname[32];
    char     band[8];       // "2.4G" or "5G"
    int8_t   rssi;
    bool     online;
    bool     blocked;
    uint64_t dlBytes;
    uint64_t ulBytes;
    uint32_t lastSeen;
    uint8_t  netbiosTries;
};

class DeviceManager {
public:
    void begin();
    void loop();

    void updateDevice(const String& mac, const String& ip, const String& hostname,
                      const String& band, int8_t rssi, bool online,
                      uint64_t dl = 0, uint64_t ul = 0);

    bool setBlocked(const String& mac, bool blocked);
    bool isBlocked(const String& mac) const;

    String queryNetBIOS(const String& ipStr);

    String getDevicesJson() const;
    size_t getConnectedCount() const;
    size_t getTotalCount() const;

private:
    int _findDeviceIndex(const String& mac) const;
    void _saveBlockedMacs();
    void _loadBlockedMacs();

    ClientDevice        _devices[MAX_TRACKED_DEVICES];
    size_t              _deviceCount = 0;
    SemaphoreHandle_t   _mutex = nullptr;
    unsigned long       _lastNetbiosTick = 0;
    size_t              _netbiosScanIndex = 0;
};

extern DeviceManager deviceManager;

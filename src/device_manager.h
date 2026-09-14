#pragma once

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "restriction_policy.h"

// ╔══════════════════════════════════════════════════════════════╗
// ║  DeviceManager — Active Clients, NetBIOS & Access Control   ║
// ╚══════════════════════════════════════════════════════════════╝

#define MAX_TRACKED_DEVICES 64

struct ClientDevice {
    char     mac[18];
    char     ip[16];
    char     hostname[32];
    char     band[8];       // "2.4G", "5G", or "Guest"
    int8_t   rssi;
    bool     online;
    bool     blocked;
    bool     parentalControl;
    uint64_t dlBytes;
    uint64_t ulBytes;
    uint32_t lastSeen;
    uint8_t  netbiosTries;
    uint64_t hourlyUsageBytes;
    uint64_t dailyUsageBytes;
    uint32_t usageHourKey;
    uint32_t usageDayKey;
    uint8_t  hourlyLimitHitCount;
};

class DeviceManager {
public:
    void begin();
    void loop();

    void updateDevice(const String& mac, const String& ip, const String& hostname,
                      const String& band, int8_t rssi, bool online,
                      uint64_t dl = 0, uint64_t ul = 0);

    // Active network discovery via incoming DNS queries & ARP
    void registerClientActivity(const char* ip, const char* hostnameHint = nullptr);

    bool setBlocked(const String& mac, bool blocked);
    bool isBlocked(const String& mac) const;
    bool setParentalControl(const String& mac, bool enabled);
    bool isParentalControl(const String& mac) const;
    bool isClientRestricted(const String& ip) const;
    ClientRestrictionReason getClientRestrictionReason(const String& ip) const;
    bool getClientDetailsByIp(const String& ip, String& outMac, String& outHostname, uint64_t& outDailyBytes);

    String queryNetBIOS(const String& ipStr);

    String getDevicesJson() const;
    void getGuestUsage(uint64_t* rxBytes, uint64_t* txBytes) const;
    size_t getConnectedCount() const;
    size_t getTotalCount() const;

    void clearAllUsage();

    // ── 7-Day Guest Analytics ────────────────────────────────────
    String getGuestAnalyticsJson() const;
    bool deleteGuestAnalyticsRecord(const String& mac);
    void clearGuestUsage();

private:
    int  _findDeviceIndex(const String& mac) const;
    int  _findDeviceIndexByIp(const String& ip) const;
    void _saveBlockedMacs();
    void _loadBlockedMacs();
    void _saveParentalMacs();
    void _loadParentalMacs();
    void _scanSoftAPStations();
    void _rollUsageWindows(ClientDevice& device);
    String _resolveArpMac(const char* ipStr);
    void _loadGuestHistory();
    void _saveGuestHistory();
    void _archiveDayToHistory(const ClientDevice& device, uint32_t completedEpochDay);

    struct DailyHistorySlot {
        uint32_t epochDay;
        uint64_t bytesUsed;
        uint32_t activeSeconds;
        uint8_t  quotaBlockCount;
    };

    struct Guest7DayRecord {
        char mac[18];
        char hostname[32];
        uint8_t validDaysCount;
        DailyHistorySlot days[7];
    };

    static constexpr size_t MAX_GUEST_HISTORY_RECORDS = 16;

    ClientDevice                _devices[MAX_TRACKED_DEVICES];
    size_t                      _deviceCount = 0;
    std::vector<Guest7DayRecord> _guest7DayRecords;
    SemaphoreHandle_t           _mutex = nullptr;
    unsigned long               _lastNetbiosTick = 0;
    unsigned long               _lastApScanTick = 0;
    size_t                      _netbiosScanIndex = 0;
};

extern DeviceManager deviceManager;

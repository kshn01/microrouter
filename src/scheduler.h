#pragma once

#include <Arduino.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  Scheduler — NTP Sync, Wi-Fi Curfew & Temporal Waivers     ║
// ╚══════════════════════════════════════════════════════════════╝

#define MAX_TEMP_WAIVERS 32

struct TempWaiver {
    char     mac[18];
    time_t   expireEpoch;
    bool     active;
    uint32_t lastGrantEpochDay;
    uint8_t  waiverCountToday;
};

struct CurfewSchedule {
    bool enabled;
    int  startHour;
    int  startMin;
    int  endHour;
    int  endMin;
};

struct QuotaLimits {
    bool     hourlyEnabled;
    uint64_t hourlyLimitBytes;
    bool     dailyEnabled;
    uint64_t dailyLimitBytes;
    bool     timeEnabled;
    uint32_t dailyActiveSecs;
};

class Scheduler {
public:
    void begin();
    void loop();

    // Time & NTP
    bool isTimeSynced() const;
    String getFormattedTime() const;
    String getTimeOnly() const;
    time_t getEpoch() const;

    // Temporal Waivers
    bool grantWaiver(const String& mac, uint32_t durationSecs, bool isAdminOverride = false);
    bool revokeWaiver(const String& mac);
    bool transferWaiver(const String& oldMac, const String& newMac);
    bool hasActiveWaiver(const String& mac) const;
    uint32_t getWaiverRemainingSecs(const String& mac) const;
    uint8_t getWaiverCountToday(const String& mac) const;
    uint8_t getMaxWaiversPerDay() const;
    void setMaxWaiversPerDay(uint8_t maxWaivers);
    bool hasParentalPin() const;
    bool verifyParentalPin(const String& pin) const;
    void setParentalPin(const String& pin);
    String getWaiversJson() const;

    // Curfew Schedules
    void getCurfew(CurfewSchedule* outSched) const;
    void setCurfew(bool enabled, int sHr, int sMn, int eHr, int eMn);
    bool isCurfewActive() const;

    // Quota Configuration
    void getQuotas(QuotaLimits* outQuotas) const;
    void setQuotas(bool hEn, uint64_t hBytes, bool dEn, uint64_t dBytes, bool tEn, uint32_t tSecs);

private:
    void _syncNTP();
    void _checkExpirations();
    void _saveWaivers();
    void _loadWaivers();

    CurfewSchedule _curfew{false, 23, 0, 6, 0};
    QuotaLimits    _quotas{false, 500 * 1024 * 1024ULL, false, 2 * 1024 * 1024 * 1024ULL, false, 3600};
    TempWaiver     _waivers[MAX_TEMP_WAIVERS];
    uint8_t        _maxWaiversPerDay = 2;
    char           _parentalPin[16] = "";
    unsigned long  _lastNtpCheck = 0;
    unsigned long  _lastLoopCheck = 0;
    bool           _ntpSynced = false;
    bool           _lastCurfewActive = false;
};

extern Scheduler scheduler;

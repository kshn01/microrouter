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
    char   mac[18];
    time_t expireEpoch;
    bool   active;
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
    time_t getEpoch() const;

    // Temporal Waivers
    bool grantWaiver(const String& mac, uint32_t durationSecs);
    bool revokeWaiver(const String& mac);
    bool hasActiveWaiver(const String& mac) const;
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
    unsigned long  _lastNtpCheck = 0;
    unsigned long  _lastLoopCheck = 0;
    bool           _ntpSynced = false;
};

extern Scheduler scheduler;

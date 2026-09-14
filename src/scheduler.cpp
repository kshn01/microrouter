#include "scheduler.h"
#include "scheduler_policy.h"
#include "config.h"
#include "zte_client.h"
#include "dns_engine.h"
#include <Preferences.h>
#include <ArduinoJson.h>

Scheduler scheduler;

void Scheduler::begin() {
    memset(_waivers, 0, sizeof(_waivers));

    Preferences prefs;
    prefs.begin("microrouter", true);
    _curfew.enabled   = prefs.getBool("sch_active", false);
    _curfew.startHour = prefs.getInt("sch_st_hr", 23);
    _curfew.startMin  = prefs.getInt("sch_st_mn", 0);
    _curfew.endHour   = prefs.getInt("sch_end_hr", 6);
    _curfew.endMin    = prefs.getInt("sch_end_mn", 0);

    _quotas.hourlyEnabled   = prefs.getBool("q_hr_en", false);
    _quotas.hourlyLimitBytes= prefs.getULong64("q_hr_bytes", 500 * 1024 * 1024ULL);
    _quotas.dailyEnabled    = prefs.getBool("q_dy_en", false);
    _quotas.dailyLimitBytes = prefs.getULong64("q_dy_bytes", 2048 * 1024 * 1024ULL);
    _quotas.timeEnabled     = prefs.getBool("q_tm_en", false);
    _quotas.dailyActiveSecs = prefs.getUInt("q_tm_secs", 3600);
    prefs.end();

    _loadWaivers();
    _syncNTP();
}

void Scheduler::loop() {
    unsigned long nowMs = millis();

    // Check every 5 seconds
    if (nowMs - _lastLoopCheck > 5000) {
        _lastLoopCheck = nowMs;
        _checkExpirations();

        // Periodically verify NTP sync if not yet valid
        if (!_ntpSynced) {
            time_t now = time(nullptr);
            if (now > 1700000000) {
                _ntpSynced = true;
                _lastCurfewActive = isCurfewActive();
                Serial.printf("[Scheduler] NTP synced: %s", ctime(&now));
            } else if (nowMs - _lastNtpCheck > 30000) {
                _lastNtpCheck = nowMs;
                _syncNTP();
            }
        }

        // Automated Curfew Edge Detection: Trigger ZTE Router DHCP update when entering/exiting curfew
        if (_ntpSynced) {
            bool currentCurfewActive = isCurfewActive();
            if (currentCurfewActive != _lastCurfewActive) {
                Serial.printf("[Scheduler] Curfew transition detected: %s -> %s. Syncing ZTE router DHCP...\n",
                              _lastCurfewActive ? "ACTIVE" : "INACTIVE",
                              currentCurfewActive ? "ACTIVE" : "INACTIVE");
                _lastCurfewActive = currentCurfewActive;

                // Sync with ZTE Router:
                // When curfew active: enforce strict zero-bypass mode (both DNS1 & DNS2 = MicroRouter IP)
                // When curfew ends: restore user's saved high-availability mode
                Preferences prefs;
                prefs.begin("microrouter", true);
                bool userHa = prefs.getBool("dns_user_ha", true);
                prefs.end();

                DnsStatsSnapshot stats;
                dnsEngine.getStats(&stats);
                bool targetHa = currentCurfewActive ? false : userHa;
                zteClient.syncRouterDnsProfile(stats.profileKey, stats.upstreamPrimary, stats.upstreamSecondary, targetHa);
            }
        }
    }
}

void Scheduler::_syncNTP() {
    // Standard IST offset (UTC+5:30 = 19800 seconds)
    configTime(19800, 0, "pool.ntp.org", "time.google.com");
    time_t now = time(nullptr);
    if (now > 1700000000) { // Sane epoch after ~2023
        _ntpSynced = true;
        Serial.printf("[Scheduler] NTP synced: %s", ctime(&now));
    }
}

bool Scheduler::isTimeSynced() const {
    time_t now = time(nullptr);
    return (_ntpSynced || now > 1700000000) && now > 1700000000;
}

time_t Scheduler::getEpoch() const {
    return time(nullptr);
}

String Scheduler::getFormattedTime() const {
    time_t now = time(nullptr);
    if (now < 1700000000) return "Not Synced";
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    return String(buf);
}

String Scheduler::getTimeOnly() const {
    time_t now = time(nullptr);
    if (now < 1700000000) return "--:--";
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &ti);
    return String(buf);
}

bool Scheduler::grantWaiver(const String& mac, uint32_t durationSecs, bool isAdminOverride) {
    time_t now = time(nullptr);
    uint32_t currentDay = (now > 1700000000) ? (uint32_t)(now / 86400) : 0;
    time_t exp = now + durationSecs;

    // Search existing
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            // Check day rollover
            if (_waivers[i].lastGrantEpochDay != currentDay) {
                _waivers[i].waiverCountToday = 0;
                _waivers[i].lastGrantEpochDay = currentDay;
            }

            if (!isAdminOverride && !canGrantWaiver(_waivers[i].waiverCountToday, _maxWaiversPerDay)) {
                Serial.printf("[Scheduler] Waiver denied for %s: daily cap (%u/%u) reached\n",
                              mac.c_str(), _waivers[i].waiverCountToday, _maxWaiversPerDay);
                return false;
            }

            _waivers[i].expireEpoch = exp;
            _waivers[i].active = true;
            if (!isAdminOverride) {
                _waivers[i].waiverCountToday++;
            }
            _saveWaivers();
            Serial.printf("[Scheduler] Extended waiver for %s until %lu (used %u/%u today)\n",
                          mac.c_str(), (unsigned long)exp, _waivers[i].waiverCountToday, _maxWaiversPerDay);
            return true;
        }
    }

    // Allocate slot
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (!_waivers[i].active) {
            strncpy(_waivers[i].mac, mac.c_str(), sizeof(_waivers[i].mac) - 1);
            _waivers[i].mac[sizeof(_waivers[i].mac) - 1] = '\0';
            _waivers[i].expireEpoch = exp;
            _waivers[i].active = true;
            _waivers[i].lastGrantEpochDay = currentDay;
            _waivers[i].waiverCountToday = isAdminOverride ? 0 : 1;
            _saveWaivers();
            Serial.printf("[Scheduler] Granted waiver for %s (+%u s, used %u/%u today)\n",
                          mac.c_str(), durationSecs, _waivers[i].waiverCountToday, _maxWaiversPerDay);
            return true;
        }
    }

    return false;
}

bool Scheduler::revokeWaiver(const String& mac) {
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            _waivers[i].active = false;
            _saveWaivers();
            Serial.printf("[Scheduler] Revoked waiver for %s\n", mac.c_str());
            return true;
        }
    }
    return false;
}

bool Scheduler::transferWaiver(const String& oldMac, const String& newMac) {
    if (oldMac.length() == 0 || newMac.length() == 0 || oldMac.equalsIgnoreCase(newMac)) return false;

    int oldIdx = -1;
    int newIdx = -1;
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (strcasecmp(_waivers[i].mac, oldMac.c_str()) == 0) oldIdx = i;
        if (strcasecmp(_waivers[i].mac, newMac.c_str()) == 0) newIdx = i;
    }
    if (oldIdx < 0) return false;

    if (newIdx >= 0) {
        if (_waivers[oldIdx].expireEpoch > _waivers[newIdx].expireEpoch) {
            _waivers[newIdx].expireEpoch = _waivers[oldIdx].expireEpoch;
        }
        if (_waivers[oldIdx].waiverCountToday > _waivers[newIdx].waiverCountToday) {
            _waivers[newIdx].waiverCountToday = _waivers[oldIdx].waiverCountToday;
        }
        if (_waivers[oldIdx].active) {
            _waivers[newIdx].active = true;
        }
        _waivers[oldIdx].active = false;
        _waivers[oldIdx].mac[0] = '\0';
    } else {
        strncpy(_waivers[oldIdx].mac, newMac.c_str(), sizeof(_waivers[oldIdx].mac) - 1);
        _waivers[oldIdx].mac[sizeof(_waivers[oldIdx].mac) - 1] = '\0';
    }
    _saveWaivers();
    Serial.printf("[Scheduler] Transferred waiver from %s to %s\n", oldMac.c_str(), newMac.c_str());
    return true;
}

bool Scheduler::hasActiveWaiver(const String& mac) const {
    time_t now = time(nullptr);
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            if (_waivers[i].expireEpoch > now) return true;
        }
    }
    return false;
}

uint32_t Scheduler::getWaiverRemainingSecs(const String& mac) const {
    time_t now = time(nullptr);
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && strcasecmp(_waivers[i].mac, mac.c_str()) == 0 &&
            _waivers[i].expireEpoch > now) {
            return (uint32_t)(_waivers[i].expireEpoch - now);
        }
    }
    return 0;
}

uint8_t Scheduler::getWaiverCountToday(const String& mac) const {
    time_t now = time(nullptr);
    uint32_t currentDay = (now > 1700000000) ? (uint32_t)(now / 86400) : 0;
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            if (_waivers[i].lastGrantEpochDay != currentDay) {
                return 0;
            }
            return _waivers[i].waiverCountToday;
        }
    }
    return 0;
}

uint8_t Scheduler::getMaxWaiversPerDay() const {
    return _maxWaiversPerDay;
}

void Scheduler::setMaxWaiversPerDay(uint8_t maxWaivers) {
    _maxWaiversPerDay = maxWaivers;
    _saveWaivers();
}

void Scheduler::_checkExpirations() {
    time_t now = time(nullptr);
    bool changed = false;

    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && now >= _waivers[i].expireEpoch) {
            _waivers[i].active = false;
            changed = true;
            Serial.printf("[Scheduler] Waiver expired for %s\n", _waivers[i].mac);
        }
    }

    if (changed) _saveWaivers();
}

void Scheduler::_saveWaivers() {
    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putBytes("waivers", _waivers, sizeof(_waivers));
    prefs.putUChar("w_max_day", _maxWaiversPerDay);
    prefs.end();
}

void Scheduler::_loadWaivers() {
    Preferences prefs;
    prefs.begin("microrouter", true);
    size_t len = prefs.getBytesLength("waivers");
    if (len == sizeof(_waivers)) {
        prefs.getBytes("waivers", _waivers, sizeof(_waivers));
    } else {
        memset(_waivers, 0, sizeof(_waivers));
    }
    _maxWaiversPerDay = prefs.getUChar("w_max_day", 2);
    prefs.end();
}

String Scheduler::getWaiversJson() const {
    JsonDocument doc;
    JsonArray arr = doc["waivers"].to<JsonArray>();
    doc["maxPerDay"] = _maxWaiversPerDay;
    time_t now = time(nullptr);

    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && _waivers[i].expireEpoch > now) {
            JsonObject w = arr.add<JsonObject>();
            w["mac"]          = _waivers[i].mac;
            w["expireEpoch"]  = (uint32_t)_waivers[i].expireEpoch;
            w["remainingSecs"]= (uint32_t)(_waivers[i].expireEpoch - now);
            w["usedToday"]    = _waivers[i].waiverCountToday;
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void Scheduler::getCurfew(CurfewSchedule* outSched) const {
    if (outSched) *outSched = _curfew;
}

void Scheduler::setCurfew(bool enabled, int sHr, int sMn, int eHr, int eMn) {
    _curfew.enabled   = enabled;
    _curfew.startHour = sHr;
    _curfew.startMin  = sMn;
    _curfew.endHour   = eHr;
    _curfew.endMin    = eMn;

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putBool("sch_active", enabled);
    prefs.putInt("sch_st_hr", sHr);
    prefs.putInt("sch_st_mn", sMn);
    prefs.putInt("sch_end_hr", eHr);
    prefs.putInt("sch_end_mn", eMn);
    prefs.end();

    Serial.printf("[Scheduler] Curfew updated: %02d:%02d to %02d:%02d (Enabled: %d)\n",
                  sHr, sMn, eHr, eMn, enabled);
}

bool Scheduler::isCurfewActive() const {
    if (!_curfew.enabled || !isTimeSynced()) return false;

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    int curMin = ti.tm_hour * 60 + ti.tm_min;
    int startMin = _curfew.startHour * 60 + _curfew.startMin;
    int endMin = _curfew.endHour * 60 + _curfew.endMin;

    return isScheduleActive(_curfew.enabled, isTimeSynced(), curMin, startMin, endMin);
}

void Scheduler::getQuotas(QuotaLimits* outQuotas) const {
    if (outQuotas) *outQuotas = _quotas;
}

void Scheduler::setQuotas(bool hEn, uint64_t hBytes, bool dEn, uint64_t dBytes, bool tEn, uint32_t tSecs) {
    _quotas.hourlyEnabled    = hEn;
    _quotas.hourlyLimitBytes = hBytes;
    _quotas.dailyEnabled     = dEn;
    _quotas.dailyLimitBytes  = dBytes;
    _quotas.timeEnabled      = tEn;
    _quotas.dailyActiveSecs  = tSecs;

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putBool("q_hr_en", hEn);
    prefs.putULong64("q_hr_bytes", hBytes);
    prefs.putBool("q_dy_en", dEn);
    prefs.putULong64("q_dy_bytes", dBytes);
    prefs.putBool("q_tm_en", tEn);
    prefs.putUInt("q_tm_secs", tSecs);
    prefs.end();

    Serial.println("[Scheduler] Quotas updated.");
}

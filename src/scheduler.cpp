#include "scheduler.h"
#include "config.h"
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
        if (!_ntpSynced && (nowMs - _lastNtpCheck > 30000)) {
            _lastNtpCheck = nowMs;
            _syncNTP();
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
    return _ntpSynced && time(nullptr) > 1700000000;
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

bool Scheduler::grantWaiver(const String& mac, uint32_t durationSecs) {
    time_t now = time(nullptr);
    time_t exp = now + durationSecs;

    // Search existing
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            _waivers[i].expireEpoch = exp;
            _saveWaivers();
            Serial.printf("[Scheduler] Extended waiver for %s until %lu\n", mac.c_str(), (unsigned long)exp);
            return true;
        }
    }

    // Allocate slot
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (!_waivers[i].active) {
            strncpy(_waivers[i].mac, mac.c_str(), sizeof(_waivers[i].mac) - 1);
            _waivers[i].expireEpoch = exp;
            _waivers[i].active = true;
            _saveWaivers();
            Serial.printf("[Scheduler] Granted waiver for %s (+%u s)\n", mac.c_str(), durationSecs);
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

bool Scheduler::hasActiveWaiver(const String& mac) const {
    time_t now = time(nullptr);
    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && strcasecmp(_waivers[i].mac, mac.c_str()) == 0) {
            if (_waivers[i].expireEpoch > now) return true;
        }
    }
    return false;
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
    prefs.end();
}

void Scheduler::_loadWaivers() {
    Preferences prefs;
    prefs.begin("microrouter", true);
    size_t len = prefs.getBytesLength("waivers");
    if (len == sizeof(_waivers)) {
        prefs.getBytes("waivers", _waivers, sizeof(_waivers));
    }
    prefs.end();
}

String Scheduler::getWaiversJson() const {
    JsonDocument doc;
    JsonArray arr = doc["waivers"].to<JsonArray>();
    time_t now = time(nullptr);

    for (int i = 0; i < MAX_TEMP_WAIVERS; i++) {
        if (_waivers[i].active && _waivers[i].expireEpoch > now) {
            JsonObject w = arr.add<JsonObject>();
            w["mac"]          = _waivers[i].mac;
            w["expireEpoch"]  = (uint32_t)_waivers[i].expireEpoch;
            w["remainingSecs"]= (uint32_t)(_waivers[i].expireEpoch - now);
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

    if (startMin <= endMin) {
        return curMin >= startMin && curMin < endMin;
    } else {
        // Crosses midnight (e.g. 23:00 to 06:00)
        return curMin >= startMin || curMin < endMin;
    }
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

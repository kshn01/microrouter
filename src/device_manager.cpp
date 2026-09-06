#include "device_manager.h"
#include "scheduler.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/etharp.h>
#include <esp_wifi.h>

DeviceManager deviceManager;

void DeviceManager::begin() {
    _mutex = xSemaphoreCreateMutex();
    _deviceCount = 0;
    _lastNetbiosTick = millis();
    _lastApScanTick = millis();
    _netbiosScanIndex = 0;

    _loadBlockedMacs();
    _loadGuestHistory();
    Serial.println("[DeviceManager] Initialized with active network discovery & ARP monitoring.");
}

void DeviceManager::loop() {
    unsigned long now = millis();

    // 1. SoftAP Station Scanner (Every 5 seconds)
    if (now - _lastApScanTick > 5000) {
        _lastApScanTick = now;
        _scanSoftAPStations();
    }

    // 2. NetBIOS Probe Round-Robin (Every 10 seconds)
    if (now - _lastNetbiosTick > 10000 && _deviceCount > 0) {
        _lastNetbiosTick = now;

        String probeIp = "";
        int targetIdx = -1;

        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        for (size_t i = 0; i < _deviceCount; i++) {
            size_t idx = (_netbiosScanIndex + i) % _deviceCount;
            if (_devices[idx].online &&
                (_devices[idx].hostname[0] == '\0' || strcmp(_devices[idx].hostname, "Unknown") == 0) &&
                _devices[idx].netbiosTries < 4 &&
                strlen(_devices[idx].ip) > 6) {
                probeIp = _devices[idx].ip;
                _devices[idx].netbiosTries++;
                targetIdx = idx;
                _netbiosScanIndex = (idx + 1) % _deviceCount;
                break;
            }
        }
        if (_mutex) xSemaphoreGive(_mutex);

        // Perform UDP probe outside mutex lock to prevent blocking web requests
        if (probeIp.length() > 0) {
            String resolved = queryNetBIOS(probeIp);
            if (resolved.length() > 0 && targetIdx >= 0) {
                if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
                if ((size_t)targetIdx < _deviceCount) {
                    strncpy(_devices[targetIdx].hostname, resolved.c_str(), sizeof(_devices[targetIdx].hostname) - 1);
                    _devices[targetIdx].hostname[sizeof(_devices[targetIdx].hostname) - 1] = '\0';
                }
                if (_mutex) xSemaphoreGive(_mutex);
            }
        }
    }
}

String DeviceManager::_resolveArpMac(const char* ipStr) {
    if (!ipStr || ipStr[0] == '\0') return "";
    ip4_addr_t ipaddr;
    if (!ip4addr_aton(ipStr, &ipaddr)) return "";
    struct eth_addr* eth_ret = NULL;
    const ip4_addr_t* ip_ret = NULL;
    if (netif_default && etharp_find_addr(netif_default, &ipaddr, &eth_ret, &ip_ret) >= 0 && eth_ret) {
        char macBuf[18];
        snprintf(macBuf, sizeof(macBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                 eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                 eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
        return String(macBuf);
    }
    return "";
}

void DeviceManager::_scanSoftAPStations() {
    wifi_sta_list_t staList;
    tcpip_adapter_sta_list_t adapterStaList;

    if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK) {
        tcpip_adapter_get_sta_list(&staList, &adapterStaList);

        for (int i = 0; i < adapterStaList.num; i++) {
            tcpip_adapter_sta_info_t station = adapterStaList.sta[i];
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                     station.mac[0], station.mac[1], station.mac[2],
                     station.mac[3], station.mac[4], station.mac[5]);

            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                     esp_ip4_addr1(&station.ip),
                     esp_ip4_addr2(&station.ip),
                     esp_ip4_addr3(&station.ip),
                     esp_ip4_addr4(&station.ip));

            int8_t rssi = -60; // default estimated RSSI for AP stations
            updateDevice(macStr, ipStr, "", "2.4G", rssi, true);
        }
    }
}

void DeviceManager::registerClientActivity(const char* ip, const char* hostnameHint) {
    if (!ip || ip[0] == '\0' || strcmp(ip, "0.0.0.0") == 0 || strcmp(ip, "127.0.0.1") == 0) return;

    // Check if this is the ESP32 itself
    if (WiFi.isConnected() && WiFi.localIP().toString().equals(ip)) return;
    if (WiFi.softAPIP().toString().equals(ip)) return;

    String mac = _resolveArpMac(ip);
    if (mac.length() == 0) {
        // Synthesize a stable, locally-administered MAC from IPv4 octets
        int o1, o2, o3, o4;
        if (sscanf(ip, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) == 4) {
            char synMac[18];
            snprintf(synMac, sizeof(synMac), "02:00:%02x:%02x:%02x:%02x", o1, o2, o3, o4);
            mac = synMac;
        } else {
            return;
        }
    }

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    if (idx < 0) {
        idx = _findDeviceIndexByIp(ip);
    }

    uint32_t nowSec = millis() / 1000;

    if (idx >= 0) {
        _devices[idx].online = true;
        _devices[idx].lastSeen = nowSec;
        _devices[idx].dlBytes += 128; // Record telemetry packet transfer
        _devices[idx].ulBytes += 64;
        _devices[idx].hourlyUsageBytes += 192;
        _devices[idx].dailyUsageBytes += 192;

        if (strlen(_devices[idx].ip) == 0 || strcmp(_devices[idx].ip, "0.0.0.0") == 0) {
            strncpy(_devices[idx].ip, ip, sizeof(_devices[idx].ip) - 1);
        }
        if (hostnameHint && strlen(hostnameHint) > 0 &&
            (_devices[idx].hostname[0] == '\0' || strcmp(_devices[idx].hostname, "Unknown") == 0)) {
            // Extract top-level domain as hint if appropriate
            const char* dot = strrchr(hostnameHint, '.');
            if (!dot || (dot - hostnameHint < 30)) {
                // Don't set full FQDNs like api.facebook.com as hostname, but keep local names
                if (strstr(hostnameHint, ".local") != nullptr) {
                    strncpy(_devices[idx].hostname, hostnameHint, sizeof(_devices[idx].hostname) - 1);
                }
            }
        }
    } else if (_deviceCount < MAX_TRACKED_DEVICES) {
        idx = _deviceCount++;
        strncpy(_devices[idx].mac, mac.c_str(), sizeof(_devices[idx].mac) - 1);
        _devices[idx].mac[sizeof(_devices[idx].mac) - 1] = '\0';
        strncpy(_devices[idx].ip, ip, sizeof(_devices[idx].ip) - 1);
        _devices[idx].ip[sizeof(_devices[idx].ip) - 1] = '\0';
        _devices[idx].hostname[0] = '\0';
        strcpy(_devices[idx].band, "2.4G");
        _devices[idx].rssi = -65;
        _devices[idx].online = true;
        _devices[idx].blocked = false;
        _devices[idx].dlBytes = 256;
        _devices[idx].ulBytes = 128;
        _devices[idx].hourlyUsageBytes = 384;
        _devices[idx].dailyUsageBytes = 384;
        _devices[idx].hourlyLimitHitCount = 0;
        _devices[idx].lastSeen = nowSec;
        _devices[idx].netbiosTries = 0;
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

void DeviceManager::updateDevice(const String& mac, const String& ip, const String& hostname,
                                const String& band, int8_t rssi, bool online,
                                uint64_t dl, uint64_t ul) {
    if (mac.length() == 0) return;

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    uint32_t nowSec = millis() / 1000;

    if (idx >= 0) {
        if (ip.length() > 0) strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
        if (hostname.length() > 0) strncpy(_devices[idx].hostname, hostname.c_str(), sizeof(_devices[idx].hostname) - 1);
        if (band.length() > 0) strncpy(_devices[idx].band, band.c_str(), sizeof(_devices[idx].band) - 1);
        if (rssi != 0) _devices[idx].rssi = rssi;
        _devices[idx].online = online;
        if (dl > 0) _devices[idx].dlBytes = dl;
        if (ul > 0) _devices[idx].ulBytes = ul;
        _devices[idx].lastSeen = nowSec;
    } else if (_deviceCount < MAX_TRACKED_DEVICES) {
        idx = _deviceCount++;
        strncpy(_devices[idx].mac, mac.c_str(), sizeof(_devices[idx].mac) - 1);
        strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
        strncpy(_devices[idx].hostname, hostname.c_str(), sizeof(_devices[idx].hostname) - 1);
        strncpy(_devices[idx].band, band.c_str(), sizeof(_devices[idx].band) - 1);
        _devices[idx].rssi = rssi;
        _devices[idx].online = online;
        _devices[idx].blocked = false;
        _devices[idx].dlBytes = dl;
        _devices[idx].ulBytes = ul;
        _devices[idx].hourlyUsageBytes = 0;
        _devices[idx].dailyUsageBytes = 0;
        _devices[idx].hourlyLimitHitCount = 0;
        _devices[idx].lastSeen = nowSec;
        _devices[idx].netbiosTries = 0;
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

int DeviceManager::_findDeviceIndex(const String& mac) const {
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcasecmp(_devices[i].mac, mac.c_str()) == 0) return i;
    }
    return -1;
}

int DeviceManager::_findDeviceIndexByIp(const String& ip) const {
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcmp(_devices[i].ip, ip.c_str()) == 0) return i;
    }
    return -1;
}

bool DeviceManager::setBlocked(const String& mac, bool blocked) {
    bool found = false;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    if (idx >= 0) {
        _devices[idx].blocked = blocked;
        found = true;
    }
    if (_mutex) xSemaphoreGive(_mutex);

    _saveBlockedMacs();
    return found;
}

bool DeviceManager::isBlocked(const String& mac) const {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    bool b = (idx >= 0) ? _devices[idx].blocked : false;
    if (_mutex) xSemaphoreGive(_mutex);
    return b;
}

bool DeviceManager::isClientRestricted(const String& ip) const {
    if (ip.length() == 0) return false;

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndexByIp(ip);
    if (idx < 0) {
        if (_mutex) xSemaphoreGive(_mutex);
        return false;
    }

    const ClientDevice& device = _devices[idx];
    bool hasWaiver = scheduler.hasActiveWaiver(device.mac);
    bool restricted = device.blocked || (!hasWaiver && scheduler.isCurfewActive());
    QuotaLimits quotas;
    scheduler.getQuotas(&quotas);
    if (!hasWaiver && quotas.hourlyEnabled && device.hourlyUsageBytes >= quotas.hourlyLimitBytes) restricted = true;
    if (!hasWaiver && quotas.dailyEnabled && device.dailyUsageBytes >= quotas.dailyLimitBytes) restricted = true;
    if (_mutex) xSemaphoreGive(_mutex);
    return restricted;
}

void DeviceManager::_saveBlockedMacs() {
    Preferences prefs;
    if (prefs.begin("dev_block", false)) {
        String blockedList = "";
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        for (size_t i = 0; i < _deviceCount; i++) {
            if (_devices[i].blocked) {
                if (blockedList.length() > 0) blockedList += ",";
                blockedList += _devices[i].mac;
            }
        }
        if (_mutex) xSemaphoreGive(_mutex);
        prefs.putString("macs", blockedList);
        prefs.end();
    }
}

void DeviceManager::_loadBlockedMacs() {
    Preferences prefs;
    if (prefs.begin("dev_block", true)) {
        String list = prefs.getString("macs", "");
        prefs.end();

        if (list.length() > 0) {
            int start = 0;
            while (start < (int)list.length()) {
                int comma = list.indexOf(',', start);
                if (comma < 0) comma = list.length();
                String mac = list.substring(start, comma);
                mac.trim();
                if (mac.length() > 0) {
                    updateDevice(mac, "", "", "2.4G", -70, false);
                    int idx = _findDeviceIndex(mac);
                    if (idx >= 0) _devices[idx].blocked = true;
                }
                start = comma + 1;
            }
        }
    }
}

void DeviceManager::clearAllUsage() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        _devices[i].dlBytes = 0;
        _devices[i].ulBytes = 0;
        _devices[i].hourlyUsageBytes = 0;
        _devices[i].dailyUsageBytes = 0;
        _devices[i].hourlyLimitHitCount = 0;
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

String DeviceManager::queryNetBIOS(const String& ipStr) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return "";

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 600000; // 600ms timeout
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(137);
    inet_pton(AF_INET, ipStr.c_str(), &dest.sin_addr);

    // Standard NetBIOS Node Status Request payload
    static const uint8_t nbQuery[50] = {
        0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x20, 0x43, 0x4b, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x00, 0x00, 0x21,
        0x00, 0x01
    };

    sendto(sock, nbQuery, sizeof(nbQuery), 0, (struct sockaddr*)&dest, sizeof(dest));

    uint8_t rxBuf[256];
    socklen_t addrLen = sizeof(dest);
    int len = recvfrom(sock, rxBuf, sizeof(rxBuf), 0, (struct sockaddr*)&dest, &addrLen);
    close(sock);

    if (len > 56) {
        uint8_t numNames = rxBuf[56];
        if (numNames > 0 && len >= 57 + 18) {
            char name[16];
            memcpy(name, &rxBuf[57], 15);
            name[15] = '\0';
            for (int i = 14; i >= 0; i--) {
                if (name[i] == ' ') name[i] = '\0';
                else break;
            }
            if (strlen(name) > 0 && isprint((unsigned char)name[0])) {
                return String(name);
            }
        }
    }
    return "";
}

String DeviceManager::getDevicesJson() const {
    JsonDocument doc;
    JsonArray connArr   = doc["connected"].to<JsonArray>();
    JsonArray allArr    = doc["all"].to<JsonArray>();
    JsonArray devArr    = doc["devices"].to<JsonArray>();
    JsonArray usageArr  = doc["usage"].to<JsonArray>();
    JsonArray blockArr  = doc["blocked"].to<JsonArray>();
    JsonArray waiverArr = doc["waivers"].to<JsonArray>();

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        JsonObject d = allArr.add<JsonObject>();
        d["mac"]          = _devices[i].mac;
        d["ip"]           = _devices[i].ip;
        d["hostname"]     = _devices[i].hostname;
        d["band"]         = _devices[i].band;
        d["rssi"]         = _devices[i].rssi;
        d["online"]       = _devices[i].online;
        d["blocked"]      = _devices[i].blocked;
        d["isBlocked"]    = _devices[i].blocked;
        d["waiver"]       = scheduler.hasActiveWaiver(_devices[i].mac);
        d["rxBytes"]      = _devices[i].dlBytes;
        d["txBytes"]      = _devices[i].ulBytes;
        d["dlBytes"]      = _devices[i].dlBytes;
        d["ulBytes"]      = _devices[i].ulBytes;
        d["hourlyUsage"]  = _devices[i].hourlyUsageBytes;
        d["dailyUsage"]   = _devices[i].dailyUsageBytes;
        d["hitCount"]     = _devices[i].hourlyLimitHitCount;

        // Populate aliases so both k1174 and standard frontend formats work
        devArr.add(d);

        if (_devices[i].online) {
            connArr.add(d);
        }

        JsonObject u = usageArr.add<JsonObject>();
        u["mac"]              = _devices[i].mac;
        u["hostname"]         = _devices[i].hostname;
        u["hourlyUsageBytes"] = _devices[i].hourlyUsageBytes;
        u["dailyUsageBytes"]  = _devices[i].dailyUsageBytes;
        u["hitCount"]         = _devices[i].hourlyLimitHitCount;

        if (_devices[i].blocked) {
            JsonObject b = blockArr.add<JsonObject>();
            b["mac"] = _devices[i].mac;
        }

        if (scheduler.hasActiveWaiver(_devices[i].mac)) {
            JsonObject w = waiverArr.add<JsonObject>();
            w["mac"] = _devices[i].mac;
            w["remaining"] = 1800; // estimated remaining secs
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);

    doc["connectedCount"] = connArr.size();
    doc["totalCount"]     = allArr.size();
    doc["count"]          = allArr.size();
    doc["pollAge"]        = 1;

    String out;
    serializeJson(doc, out);
    return out;
}

size_t DeviceManager::getConnectedCount() const {
    size_t count = 0;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        if (_devices[i].online) count++;
    }
    if (_mutex) xSemaphoreGive(_mutex);
    return count;
}

size_t DeviceManager::getTotalCount() const {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t c = _deviceCount;
    if (_mutex) xSemaphoreGive(_mutex);
    return c;
}

void DeviceManager::clearGuestUsage() {
    clearAllUsage();
}

void DeviceManager::_loadGuestHistory() {
    Preferences prefs;
    if (prefs.begin("microrouter", true)) {
        size_t len = prefs.getBytesLength("g_7d");
        if (len > 0 && len % sizeof(Guest7DayRecord) == 0) {
            _guest7DayRecords.resize(len / sizeof(Guest7DayRecord));
            prefs.getBytes("g_7d", _guest7DayRecords.data(), len);
            Serial.printf("[DeviceManager] Loaded %u 7-day history records from NVS.\n", (unsigned)_guest7DayRecords.size());
        }
        prefs.end();
    }
}

void DeviceManager::_saveGuestHistory() {
    Preferences prefs;
    if (prefs.begin("microrouter", false)) {
        if (_guest7DayRecords.size() > 0) {
            prefs.putBytes("g_7d", _guest7DayRecords.data(), _guest7DayRecords.size() * sizeof(Guest7DayRecord));
        } else {
            prefs.remove("g_7d");
        }
        prefs.end();
    }
}

bool DeviceManager::deleteGuestAnalyticsRecord(const String& mac) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    bool found = false;
    for (auto it = _guest7DayRecords.begin(); it != _guest7DayRecords.end(); ) {
        if (strcasecmp(it->mac, mac.c_str()) == 0) {
            it = _guest7DayRecords.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);
    if (found) {
        _saveGuestHistory();
    }
    return found;
}

String DeviceManager::getGuestAnalyticsJson() const {
    JsonDocument doc;
    JsonArray recArr = doc["records"].to<JsonArray>();

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    // 1. Process persisted 7-day records
    for (const auto& rec : _guest7DayRecords) {
        JsonObject r = recArr.add<JsonObject>();
        r["mac"] = rec.mac;
        r["hostname"] = rec.hostname;
        r["ip"] = "Offline";
        r["currentlyOnline"] = false;
        r["todayUsageBytes"] = 0;
        r["todayActiveSecs"] = 0;
        r["validDaysCount"] = rec.validDaysCount;

        // Check if device is currently live in _devices
        for (size_t d = 0; d < _deviceCount; d++) {
            if (strcasecmp(_devices[d].mac, rec.mac) == 0) {
                r["ip"] = _devices[d].ip;
                r["currentlyOnline"] = _devices[d].online;
                r["todayUsageBytes"] = _devices[d].dailyUsageBytes;
                if (strlen(_devices[d].hostname) > 0 && strcmp(_devices[d].hostname, "Unknown") != 0) {
                    r["hostname"] = _devices[d].hostname;
                }
                break;
            }
        }

        JsonArray histArr = r["history"].to<JsonArray>();
        for (int day = 0; day < 7; day++) {
            JsonObject h = histArr.add<JsonObject>();
            h["dayIndex"] = day + 1;
            h["epochDay"] = rec.days[day].epochDay;
            h["bytesUsed"] = rec.days[day].bytesUsed;
            h["activeSecs"] = rec.days[day].activeSeconds;
            h["quotaBlockCount"] = rec.days[day].quotaBlockCount;
        }
    }

    // 2. Add currently active devices not yet in historical record
    for (size_t d = 0; d < _deviceCount; d++) {
        bool inRecords = false;
        for (const auto& rec : _guest7DayRecords) {
            if (strcasecmp(rec.mac, _devices[d].mac) == 0) {
                inRecords = true;
                break;
            }
        }
        if (!inRecords) {
            JsonObject r = recArr.add<JsonObject>();
            r["mac"] = _devices[d].mac;
            r["hostname"] = strlen(_devices[d].hostname) > 0 ? _devices[d].hostname : "Guest Device";
            r["ip"] = _devices[d].ip;
            r["currentlyOnline"] = _devices[d].online;
            r["todayUsageBytes"] = _devices[d].dailyUsageBytes;
            r["todayActiveSecs"] = _devices[d].online ? 1800 : 0;
            r["validDaysCount"] = 1;
            JsonArray histArr = r["history"].to<JsonArray>();
            for (int day = 0; day < 7; day++) {
                JsonObject h = histArr.add<JsonObject>();
                h["dayIndex"] = day + 1;
                h["epochDay"] = 0;
                h["bytesUsed"] = 0;
                h["activeSecs"] = 0;
                h["quotaBlockCount"] = 0;
            }
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);

    String out;
    serializeJson(doc, out);
    return out;
}


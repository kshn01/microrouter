#include "device_manager.h"
#include "scheduler.h"
#include "usage_policy.h"
#include "restriction_policy.h"
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
    _loadParentalMacs();
    _loadGuestHistory();

    // Consolidate any duplicate profiles from initial load
    if (_deviceCount > 1) {
        bool merged = false;
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        for (size_t i = 0; i < _deviceCount; i++) {
            _checkHostnameMerge(i);
            merged = true;
        }
        if (_mutex) xSemaphoreGive(_mutex);
        if (merged) {
            _saveParentalMacs();
            _saveBlockedMacs();
        }
    }

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
                bool merged = false;
                if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
                if ((size_t)targetIdx < _deviceCount) {
                    strncpy(_devices[targetIdx].hostname, resolved.c_str(), sizeof(_devices[targetIdx].hostname) - 1);
                    _devices[targetIdx].hostname[sizeof(_devices[targetIdx].hostname) - 1] = '\0';
                    _checkHostnameMerge(targetIdx);
                    merged = true;
                }
                if (_mutex) xSemaphoreGive(_mutex);
                if (merged) {
                    _saveParentalMacs();
                    _saveBlockedMacs();
                }
            }
        }
    }
}

String DeviceManager::_resolveArpMac(const char* ipStr) const {
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

            updateDevice(macStr, ipStr, "", "Guest", 0, true);
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
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        int existingIdx = _findDeviceIndexByIp(ip);
        if (existingIdx >= 0) {
            mac = _devices[existingIdx].mac;
        }
        if (_mutex) xSemaphoreGive(_mutex);
    }

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
        _rollUsageWindows(_devices[idx]);
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
                    _devices[idx].hostname[sizeof(_devices[idx].hostname) - 1] = '\0';
                    _checkHostnameMerge(idx);
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
        bool isSoftApSubnet = (strncmp(ip, "192.168.4.", 10) == 0);
        strcpy(_devices[idx].band, isSoftApSubnet ? "Guest" : "2.4G");
        _devices[idx].rssi = 0;
        _devices[idx].online = true;
        _devices[idx].blocked = false;
        _devices[idx].dlBytes = 128;
        _devices[idx].ulBytes = 64;
        _devices[idx].hourlyUsageBytes = 192;
        _devices[idx].dailyUsageBytes = 192;
        _devices[idx].usageHourKey = 0;
        _devices[idx].usageDayKey = 0;
        _devices[idx].hourlyLimitHitCount = 0;
        _devices[idx].lastSeen = nowSec;
        _devices[idx].netbiosTries = 0;
        _rollUsageWindows(_devices[idx]);
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

void DeviceManager::_recordTombstone(const char* oldMac, const char* canonicalMac) {
    if (!oldMac || !canonicalMac || strcasecmp(oldMac, canonicalMac) == 0) return;
    uint32_t nowSec = millis() / 1000;

    // Check if oldMac is already in tombstones
    for (size_t i = 0; i < MAX_TOMBSTONES; i++) {
        if (_tombstones[i].oldMac[0] != '\0' && strcasecmp(_tombstones[i].oldMac, oldMac) == 0) {
            strncpy(_tombstones[i].canonicalMac, canonicalMac, sizeof(_tombstones[i].canonicalMac) - 1);
            _tombstones[i].canonicalMac[sizeof(_tombstones[i].canonicalMac) - 1] = '\0';
            _tombstones[i].retiredAtSec = nowSec;
            return;
        }
    }

    // Transitive aliasing: update any existing tombstones that pointed to oldMac
    for (size_t i = 0; i < MAX_TOMBSTONES; i++) {
        if (_tombstones[i].oldMac[0] != '\0' && strcasecmp(_tombstones[i].canonicalMac, oldMac) == 0) {
            strncpy(_tombstones[i].canonicalMac, canonicalMac, sizeof(_tombstones[i].canonicalMac) - 1);
            _tombstones[i].canonicalMac[sizeof(_tombstones[i].canonicalMac) - 1] = '\0';
        }
    }

    // Insert new tombstone
    strncpy(_tombstones[_tombstoneHead].oldMac, oldMac, sizeof(_tombstones[_tombstoneHead].oldMac) - 1);
    _tombstones[_tombstoneHead].oldMac[sizeof(_tombstones[_tombstoneHead].oldMac) - 1] = '\0';
    strncpy(_tombstones[_tombstoneHead].canonicalMac, canonicalMac, sizeof(_tombstones[_tombstoneHead].canonicalMac) - 1);
    _tombstones[_tombstoneHead].canonicalMac[sizeof(_tombstones[_tombstoneHead].canonicalMac) - 1] = '\0';
    _tombstones[_tombstoneHead].retiredAtSec = nowSec;
    _tombstoneHead = (_tombstoneHead + 1) % MAX_TOMBSTONES;
    Serial.printf("[DeviceManager] Anti-MAC: Registered alias tombstone %s -> %s\n", oldMac, canonicalMac);
}

const char* DeviceManager::_findTombstoneCanonical(const char* mac) const {
    if (!mac || mac[0] == '\0') return nullptr;
    for (size_t i = 0; i < MAX_TOMBSTONES; i++) {
        if (_tombstones[i].oldMac[0] != '\0' && strcasecmp(_tombstones[i].oldMac, mac) == 0) {
            return _tombstones[i].canonicalMac;
        }
    }
    return nullptr;
}

void DeviceManager::_mergeDeviceProfiles(size_t targetIdx, size_t sourceIdx) {
    if (targetIdx >= _deviceCount || sourceIdx >= _deviceCount || targetIdx == sourceIdx) return;

    char sourceMac[18];
    char targetMac[18];
    strncpy(sourceMac, _devices[sourceIdx].mac, sizeof(sourceMac) - 1);
    sourceMac[sizeof(sourceMac) - 1] = '\0';
    strncpy(targetMac, _devices[targetIdx].mac, sizeof(targetMac) - 1);
    targetMac[sizeof(targetMac) - 1] = '\0';

    Serial.printf("[DeviceManager] Anti-MAC: Merging profile %s (%s) into %s (%s)\n",
                  sourceMac, _devices[sourceIdx].hostname, targetMac, _devices[targetIdx].hostname);

    // 1. Accumulate usage counters into target
    _devices[targetIdx].dlBytes += _devices[sourceIdx].dlBytes;
    _devices[targetIdx].ulBytes += _devices[sourceIdx].ulBytes;
    _devices[targetIdx].hourlyUsageBytes += _devices[sourceIdx].hourlyUsageBytes;
    _devices[targetIdx].dailyUsageBytes += _devices[sourceIdx].dailyUsageBytes;
    if (_devices[sourceIdx].hourlyLimitHitCount > _devices[targetIdx].hourlyLimitHitCount) {
        _devices[targetIdx].hourlyLimitHitCount = _devices[sourceIdx].hourlyLimitHitCount;
    }

    // 2. Inherit parental control and blocked flags
    _devices[targetIdx].parentalControl = _devices[targetIdx].parentalControl || _devices[sourceIdx].parentalControl;
    _devices[targetIdx].blocked = _devices[targetIdx].blocked || _devices[sourceIdx].blocked;

    // 3. Inherit hostname if target is generic
    if (isGenericHostname(_devices[targetIdx].hostname) && !isGenericHostname(_devices[sourceIdx].hostname)) {
        strncpy(_devices[targetIdx].hostname, _devices[sourceIdx].hostname, sizeof(_devices[targetIdx].hostname) - 1);
        _devices[targetIdx].hostname[sizeof(_devices[targetIdx].hostname) - 1] = '\0';
    }

    // 4. Transfer active waivers in scheduler
    scheduler.transferWaiver(sourceMac, targetMac);

    // 5. Record tombstone alias
    _recordTombstone(sourceMac, targetMac);

    // 6. Remove source duplicate from array
    for (size_t k = sourceIdx; k + 1 < _deviceCount; k++) {
        _devices[k] = _devices[k + 1];
    }
    _deviceCount--;

    invalidateRestrictionCache();
}

void DeviceManager::_checkHostnameMerge(size_t idx) {
    if (idx >= _deviceCount) return;
    if (isGenericHostname(_devices[idx].hostname)) return;

    for (size_t j = 0; j < _deviceCount; j++) {
        if (j != idx && canMergeDeviceProfiles(_devices[j].mac, _devices[j].hostname, _devices[idx].mac, _devices[idx].hostname)) {
            _mergeDeviceProfiles(idx, j);
            break;
        }
    }
}

void DeviceManager::updateDevice(const String& mac, const String& ip, const String& hostname,
                                const String& band, int8_t rssi, bool online,
                                uint64_t dl, uint64_t ul) {
    if (mac.length() == 0) return;

    bool needsPrefSave = false;

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);

    // 0. Anti-MAC Randomization: Check if this MAC was retired as a tombstone of an active canonical MAC
    const char* canonMac = _findTombstoneCanonical(mac.c_str());
    if (canonMac != nullptr) {
        int canonIdx = _findDeviceIndex(canonMac);
        if (canonIdx >= 0) {
            // Forward any packet traffic to the active canonical device
            if (dl > 0 || ul > 0) {
                _devices[canonIdx].hourlyUsageBytes += (dl + ul);
                _devices[canonIdx].dailyUsageBytes += (dl + ul);
            }
            if (_mutex) xSemaphoreGive(_mutex);
            return;
        }
    }

    // If updating a genuine hardware MAC with an IP, clean up / merge any synthetic ("02:00:...") entry for this IP
    if (!mac.startsWith("02:00:") && ip.length() > 0) {
        for (size_t i = 0; i < _deviceCount; i++) {
            if (strcmp(_devices[i].ip, ip.c_str()) == 0 && strncmp(_devices[i].mac, "02:00:", 6) == 0) {
                int realIdx = _findDeviceIndex(mac);
                if (realIdx < 0) {
                    strncpy(_devices[i].mac, mac.c_str(), sizeof(_devices[i].mac) - 1);
                    _devices[i].mac[sizeof(_devices[i].mac) - 1] = '\0';
                } else {
                    _devices[realIdx].dlBytes += _devices[i].dlBytes;
                    _devices[realIdx].ulBytes += _devices[i].ulBytes;
                    _devices[realIdx].hourlyUsageBytes += _devices[i].hourlyUsageBytes;
                    _devices[realIdx].dailyUsageBytes += _devices[i].dailyUsageBytes;
                    for (size_t j = i; j + 1 < _deviceCount; j++) {
                        _devices[j] = _devices[j + 1];
                    }
                    _deviceCount--;
                    i--;
                }
            }
        }
    }

    int idx = _findDeviceIndex(mac);
    uint32_t nowSec = millis() / 1000;

    if (idx >= 0) {
        _rollUsageWindows(_devices[idx]);
        uint64_t previousBytes = _devices[idx].dlBytes + _devices[idx].ulBytes;
        uint64_t currentBytes = dl + ul;
        uint64_t deltaBytes = byteCounterDelta(currentBytes, previousBytes);
        if (ip.length() > 0) strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
        if (hostname.length() > 0) {
            strncpy(_devices[idx].hostname, hostname.c_str(), sizeof(_devices[idx].hostname) - 1);
            _devices[idx].hostname[sizeof(_devices[idx].hostname) - 1] = '\0';
            _checkHostnameMerge(idx);
            needsPrefSave = true;
        }
        if (band.length() > 0) {
            strncpy(_devices[idx].band, band.c_str(), sizeof(_devices[idx].band) - 1);
        }
        if (rssi != 0) _devices[idx].rssi = rssi;
        _devices[idx].online = online;
        if (dl > 0) _devices[idx].dlBytes = dl;
        if (ul > 0) _devices[idx].ulBytes = ul;
        if (dl > 0 || ul > 0) {
            _devices[idx].hourlyUsageBytes += deltaBytes;
            _devices[idx].dailyUsageBytes += deltaBytes;
        }
        _devices[idx].lastSeen = nowSec;
    } else {
        // Device not found by MAC.
        // Check if an existing device matches this hostname and should be re-keyed to the new MAC (profile takeover)
        bool mergedWithExisting = false;
        if (hostname.length() > 0 && !isGenericHostname(hostname.c_str())) {
            for (size_t i = 0; i < _deviceCount; i++) {
                if (canMergeDeviceProfiles(_devices[i].mac, _devices[i].hostname, mac.c_str(), hostname.c_str())) {
                    Serial.printf("[DeviceManager] Anti-MAC: Reconnecting %s (old MAC: %s) with new MAC %s\n",
                                  _devices[i].hostname, _devices[i].mac, mac.c_str());
                    _recordTombstone(_devices[i].mac, mac.c_str());
                    scheduler.transferWaiver(_devices[i].mac, mac);

                    strncpy(_devices[i].mac, mac.c_str(), sizeof(_devices[i].mac) - 1);
                    _devices[i].mac[sizeof(_devices[i].mac) - 1] = '\0';
                    if (ip.length() > 0) {
                        strncpy(_devices[i].ip, ip.c_str(), sizeof(_devices[i].ip) - 1);
                        _devices[i].ip[sizeof(_devices[i].ip) - 1] = '\0';
                    }
                    if (band.length() > 0) {
                        strncpy(_devices[i].band, band.c_str(), sizeof(_devices[i].band) - 1);
                        _devices[i].band[sizeof(_devices[i].band) - 1] = '\0';
                    }
                    if (rssi != 0) _devices[i].rssi = rssi;
                    _devices[i].online = online;
                    _devices[i].lastSeen = nowSec;

                    if (dl > 0 || ul > 0) {
                        _devices[i].dlBytes += dl;
                        _devices[i].ulBytes += ul;
                        _devices[i].hourlyUsageBytes += (dl + ul);
                        _devices[i].dailyUsageBytes += (dl + ul);
                    }

                    invalidateRestrictionCache();
                    mergedWithExisting = true;
                    needsPrefSave = true;
                    break;
                }
            }
        }

        if (!mergedWithExisting && _deviceCount < MAX_TRACKED_DEVICES) {
            idx = _deviceCount++;
            strncpy(_devices[idx].mac, mac.c_str(), sizeof(_devices[idx].mac) - 1);
            _devices[idx].mac[sizeof(_devices[idx].mac) - 1] = '\0';
            strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
            _devices[idx].ip[sizeof(_devices[idx].ip) - 1] = '\0';
            strncpy(_devices[idx].hostname, hostname.c_str(), sizeof(_devices[idx].hostname) - 1);
            _devices[idx].hostname[sizeof(_devices[idx].hostname) - 1] = '\0';
            strncpy(_devices[idx].band, band.c_str(), sizeof(_devices[idx].band) - 1);
            _devices[idx].band[sizeof(_devices[idx].band) - 1] = '\0';
            _devices[idx].rssi = rssi;
            _devices[idx].online = online;
            _devices[idx].blocked = false;

            Preferences pPrefs;
            bool hasSavedPref = false;
            if (pPrefs.begin("dev_parental", true)) {
                if (pPrefs.isKey("macs")) {
                    hasSavedPref = true;
                    String pList = pPrefs.getString("macs", "");
                    _devices[idx].parentalControl = (pList.indexOf(mac) >= 0);
                }
                pPrefs.end();
            }
            if (!hasSavedPref) {
                _devices[idx].parentalControl = (strcasecmp(band.c_str(), "Guest") == 0);
            }

            _devices[idx].dlBytes = dl;
            _devices[idx].ulBytes = ul;
            _devices[idx].hourlyUsageBytes = (dl + ul);
            _devices[idx].dailyUsageBytes = (dl + ul);
            _devices[idx].usageHourKey = 0;
            _devices[idx].usageDayKey = 0;
            _devices[idx].hourlyLimitHitCount = 0;
            _devices[idx].lastSeen = nowSec;
            _devices[idx].netbiosTries = 0;
            _rollUsageWindows(_devices[idx]);
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);

    if (needsPrefSave) {
        _saveParentalMacs();
        _saveBlockedMacs();
    }
}

void DeviceManager::_archiveDayToHistory(const ClientDevice& device, uint32_t completedEpochDay) {
    if (strlen(device.mac) == 0 || device.dailyUsageBytes == 0) return;

    Guest7DayRecord* rec = nullptr;
    for (auto& r : _guest7DayRecords) {
        if (strcasecmp(r.mac, device.mac) == 0) {
            rec = &r;
            break;
        }
    }

    if (!rec) {
        while (_guest7DayRecords.size() >= MAX_GUEST_HISTORY_RECORDS) {
            _guest7DayRecords.erase(_guest7DayRecords.begin());
        }
        Guest7DayRecord newRec;
        memset(&newRec, 0, sizeof(Guest7DayRecord));
        strncpy(newRec.mac, device.mac, sizeof(newRec.mac) - 1);
        strncpy(newRec.hostname, device.hostname, sizeof(newRec.hostname) - 1);
        newRec.validDaysCount = 0;
        _guest7DayRecords.push_back(newRec);
        rec = &_guest7DayRecords.back();
    }

    if (rec) {
        if (strlen(device.hostname) > 0 && strcmp(device.hostname, "Unknown") != 0) {
            strncpy(rec->hostname, device.hostname, sizeof(rec->hostname) - 1);
        }
        for (int i = 6; i > 0; i--) {
            rec->days[i] = rec->days[i - 1];
        }
        rec->days[0].epochDay = completedEpochDay;
        rec->days[0].bytesUsed = device.dailyUsageBytes;
        rec->days[0].activeSeconds = 1800;
        rec->days[0].quotaBlockCount = 0;
        if (rec->validDaysCount < 7) {
            rec->validDaysCount++;
        }
        _saveGuestHistory();
    }
}

void DeviceManager::_rollUsageWindows(ClientDevice& device) {
    uint32_t epoch = scheduler.isTimeSynced()
        ? (uint32_t)scheduler.getEpoch()
        : millis() / 1000;
    uint32_t hourKey = epoch / 3600;
    uint32_t dayKey = epoch / 86400;

    if (device.usageHourKey != 0 && device.usageHourKey != hourKey) {
        device.hourlyUsageBytes = 0;
        device.hourlyLimitHitCount = 0;
    }
    if (device.usageDayKey != 0 && device.usageDayKey != dayKey) {
        _archiveDayToHistory(device, device.usageDayKey);
        device.dailyUsageBytes = 0;
    }
    device.usageHourKey = hourKey;
    device.usageDayKey = dayKey;
}

int DeviceManager::_findDeviceIndex(const String& mac) const {
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcasecmp(_devices[i].mac, mac.c_str()) == 0) return i;
    }
    return -1;
}

int DeviceManager::_findDeviceIndexByIp(const String& ip) const {
    if (ip.length() == 0) return -1;
    int syntheticIdx = -1;
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcmp(_devices[i].ip, ip.c_str()) == 0) {
            if (strncmp(_devices[i].mac, "02:00:", 6) != 0) {
                return i;
            }
            if (syntheticIdx < 0) syntheticIdx = i;
        }
    }
    return syntheticIdx;
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

    invalidateRestrictionCache();
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

bool DeviceManager::setParentalControl(const String& mac, bool enabled) {
    bool found = false;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    if (idx >= 0) {
        _devices[idx].parentalControl = enabled;
        found = true;
    }
    if (_mutex) xSemaphoreGive(_mutex);

    invalidateRestrictionCache();
    _saveParentalMacs();
    return found;
}

bool DeviceManager::isParentalControl(const String& mac) const {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    bool enabled = false;
    if (idx >= 0) {
        enabled = _devices[idx].parentalControl;
    }
    if (_mutex) xSemaphoreGive(_mutex);
    return enabled;
}

ClientRestrictionReason DeviceManager::getClientRestrictionReason(const String& ip) const {
    if (ip.length() == 0) return RESTRICTION_NONE;

    unsigned long nowMs = millis();
    // Fast path: check 3-second cache
    for (size_t i = 0; i < RESTRICTION_CACHE_SIZE; i++) {
        if (_restrictionCache[i].expiryMs > nowMs && strcmp(_restrictionCache[i].ip, ip.c_str()) == 0) {
            return _restrictionCache[i].reason;
        }
    }

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    ClientRestrictionReason reason = RESTRICTION_NONE;
    int idx = _findDeviceIndexByIp(ip);
    if (idx >= 0) {
        QuotaLimits quotas;
        scheduler.getQuotas(&quotas);
        bool isCurfew = scheduler.isCurfewActive();

        const ClientDevice& device = _devices[idx];
        bool isTarget = device.parentalControl;
        bool hasWaiver = scheduler.hasActiveWaiver(device.mac);

        ClientRestrictionInput input = {
            .isBlocked           = device.blocked,
            .isParentalTarget    = isTarget,
            .hasActiveWaiver     = hasWaiver,
            .isCurfewActive      = isCurfew,
            .hourlyQuotaEnabled  = quotas.hourlyEnabled,
            .dailyQuotaEnabled   = quotas.dailyEnabled,
            .hourlyUsageBytes    = device.hourlyUsageBytes,
            .hourlyLimitBytes    = quotas.hourlyLimitBytes,
            .dailyUsageBytes     = device.dailyUsageBytes,
            .dailyLimitBytes     = quotas.dailyLimitBytes,
        };

        reason = evaluateClientRestrictionDetail(input);
    }
    if (_mutex) xSemaphoreGive(_mutex);

    // Update fast-path cache (TTL 3 seconds)
    size_t slot = _restrictionCacheHead % RESTRICTION_CACHE_SIZE;
    _restrictionCacheHead++;
    strncpy(_restrictionCache[slot].ip, ip.c_str(), sizeof(_restrictionCache[slot].ip) - 1);
    _restrictionCache[slot].ip[sizeof(_restrictionCache[slot].ip) - 1] = '\0';
    _restrictionCache[slot].reason = reason;
    _restrictionCache[slot].expiryMs = nowMs + 3000;

    return reason;
}

bool DeviceManager::isClientRestricted(const String& ip) const {
    return getClientRestrictionReason(ip) != RESTRICTION_NONE;
}

bool DeviceManager::getClientDetailsByIp(const String& ip, String& outMac, String& outHostname, uint64_t& outDailyBytes) const {
    if (ip.length() == 0) return false;
    bool found = false;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndexByIp(ip);
    if (idx >= 0) {
        outMac = _devices[idx].mac;
        outHostname = _devices[idx].hostname;
        outDailyBytes = _devices[idx].dailyUsageBytes;
        found = true;
    }
    if (_mutex) xSemaphoreGive(_mutex);

    if (!found) {
        String arpMac = _resolveArpMac(ip.c_str());
        if (arpMac.length() > 0) {
            outMac = arpMac;
            outHostname = "Client";
            outDailyBytes = 0;
            found = true;
        }
    }
    return found;
}

bool DeviceManager::getClientQuotaStatus(const String& ip,
                                        String& outMac,
                                        String& outHostname,
                                        uint64_t& outDailyBytes,
                                        ClientRestrictionReason& outReason,
                                        uint32_t& outWaiverRemainingSecs) const {
    if (ip.length() == 0) return false;
    outMac = "";
    outHostname = "Client";
    outDailyBytes = 0;
    outReason = RESTRICTION_NONE;
    outWaiverRemainingSecs = 0;

    bool found = false;
    QuotaLimits quotas;
    scheduler.getQuotas(&quotas);
    bool isCurfew = scheduler.isCurfewActive();

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndexByIp(ip);
    if (idx >= 0) {
        const ClientDevice& device = _devices[idx];
        outMac = device.mac;
        outHostname = device.hostname;
        outDailyBytes = device.dailyUsageBytes;
        found = true;

        bool isTarget = device.parentalControl;
        bool hasWaiver = scheduler.hasActiveWaiver(device.mac);
        outWaiverRemainingSecs = scheduler.getWaiverRemainingSecs(device.mac);

        ClientRestrictionInput input = {
            .isBlocked           = device.blocked,
            .isParentalTarget    = isTarget,
            .hasActiveWaiver     = hasWaiver,
            .isCurfewActive      = isCurfew,
            .hourlyQuotaEnabled  = quotas.hourlyEnabled,
            .dailyQuotaEnabled   = quotas.dailyEnabled,
            .hourlyUsageBytes    = device.hourlyUsageBytes,
            .hourlyLimitBytes    = quotas.hourlyLimitBytes,
            .dailyUsageBytes     = device.dailyUsageBytes,
            .dailyLimitBytes     = quotas.dailyLimitBytes,
        };
        outReason = evaluateClientRestrictionDetail(input);
    }
    if (_mutex) xSemaphoreGive(_mutex);

    if (!found) {
        String arpMac = _resolveArpMac(ip.c_str());
        if (arpMac.length() > 0) {
            outMac = arpMac;
            outHostname = "Client";
            outDailyBytes = 0;
            outWaiverRemainingSecs = scheduler.getWaiverRemainingSecs(arpMac);
            found = true;
        }
    }
    return found;
}

void DeviceManager::invalidateRestrictionCache() {
    for (size_t i = 0; i < RESTRICTION_CACHE_SIZE; i++) {
        _restrictionCache[i].expiryMs = 0;
    }
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

void DeviceManager::_saveParentalMacs() {
    Preferences prefs;
    if (prefs.begin("dev_parental", false)) {
        String parentalList = "";
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        for (size_t i = 0; i < _deviceCount; i++) {
            if (_devices[i].parentalControl) {
                if (parentalList.length() > 0) parentalList += ",";
                parentalList += _devices[i].mac;
            }
        }
        if (_mutex) xSemaphoreGive(_mutex);
        prefs.putString("macs", parentalList);
        prefs.end();
    }
}

void DeviceManager::_loadParentalMacs() {
    Preferences prefs;
    if (prefs.begin("dev_parental", true)) {
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
                    int idx = _findDeviceIndex(mac);
                    if (idx >= 0) {
                        _devices[idx].parentalControl = true;
                    }
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
        // Skip synthetic MAC entries if a real hardware MAC exists for the same IP
        if (strncmp(_devices[i].mac, "02:00:", 6) == 0) {
            bool hasReal = false;
            for (size_t j = 0; j < _deviceCount; j++) {
                if (j != i && strcmp(_devices[j].ip, _devices[i].ip) == 0 && strncmp(_devices[j].mac, "02:00:", 6) != 0) {
                    hasReal = true;
                    break;
                }
            }
            if (hasReal) continue;
        }

        JsonObject d = allArr.add<JsonObject>();
        d["mac"]          = _devices[i].mac;
        d["ip"]           = _devices[i].ip;
        d["hostname"]     = _devices[i].hostname;
        d["band"]         = _devices[i].band;
        d["rssi"]         = _devices[i].rssi;
        d["online"]       = _devices[i].online;
        d["blocked"]      = _devices[i].blocked;
        d["isBlocked"]    = _devices[i].blocked;
        d["parentalControl"] = _devices[i].parentalControl;
        d["isRandomized"] = isRandomizedMac(_devices[i].mac);
        d["waiver"]       = scheduler.hasActiveWaiver(_devices[i].mac);
        d["waiverSecRemaining"] = scheduler.getWaiverRemainingSecs(_devices[i].mac);
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
        u["isRandomized"]     = isRandomizedMac(_devices[i].mac);
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
            w["remaining"] = scheduler.getWaiverRemainingSecs(_devices[i].mac);
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

void DeviceManager::getGuestUsage(uint64_t* rxBytes, uint64_t* txBytes) const {
    uint64_t rx = 0;
    uint64_t tx = 0;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcasecmp(_devices[i].band, "Guest") == 0) {
            rx += _devices[i].dlBytes;
            tx += _devices[i].ulBytes;
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);
    if (rxBytes) *rxBytes = rx;
    if (txBytes) *txBytes = tx;
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
            size_t count = len / sizeof(Guest7DayRecord);
            if (count > MAX_GUEST_HISTORY_RECORDS) {
                count = MAX_GUEST_HISTORY_RECORDS;
            }
            _guest7DayRecords.resize(count);
            prefs.getBytes("g_7d", _guest7DayRecords.data(), count * sizeof(Guest7DayRecord));
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


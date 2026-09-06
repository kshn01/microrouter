#include "device_manager.h"
#include "config.h"
#include "scheduler.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>

DeviceManager deviceManager;

void DeviceManager::begin() {
    _mutex = xSemaphoreCreateMutex();
    memset(_devices, 0, sizeof(_devices));
    _deviceCount = 0;
    _loadBlockedMacs();
}

void DeviceManager::loop() {
    unsigned long now = millis();

    // Round-Robin NetBIOS probing every 10 seconds for unknown device names
    if (now - _lastNetbiosTick > 10000 && _deviceCount > 0) {
        _lastNetbiosTick = now;

        String targetIp = "";
        size_t targetIdx = 0;

        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        for (size_t i = 0; i < _deviceCount; i++) {
            size_t idx = (_netbiosScanIndex + i) % _deviceCount;
            if (_devices[idx].online && _devices[idx].netbiosTries < 5) {
                if (strlen(_devices[idx].hostname) == 0 ||
                    strcasecmp(_devices[idx].hostname, "Unknown") == 0) {
                    targetIp = _devices[idx].ip;
                    targetIdx = idx;
                    _devices[idx].netbiosTries++;
                    _netbiosScanIndex = (idx + 1) % _deviceCount;
                    break;
                }
            }
        }
        if (_mutex) xSemaphoreGive(_mutex);

        // Perform probe outside the lock
        if (targetIp.length() > 0) {
            String resolved = queryNetBIOS(targetIp);
            if (resolved.length() > 0) {
                if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
                strncpy(_devices[targetIdx].hostname, resolved.c_str(), sizeof(_devices[targetIdx].hostname) - 1);
                _devices[targetIdx].hostname[sizeof(_devices[targetIdx].hostname) - 1] = '\0';
                if (_mutex) xSemaphoreGive(_mutex);
                Serial.printf("[DeviceMgr] NetBIOS resolved %s -> %s\n", targetIp.c_str(), resolved.c_str());
            }
        }
    }
}

String DeviceManager::queryNetBIOS(const String& ipStr) {
    WiFiUDP udp;
    if (!udp.begin(0)) return "";

    uint8_t packet[] = {
        0x80, 0xef, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x20, 0x43, 0x4b, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x00, 0x00, 0x21, 0x00, 0x01
    };

    IPAddress ip;
    if (!ip.fromString(ipStr)) {
        udp.stop();
        return "";
    }

    udp.beginPacket(ip, 137);
    udp.write(packet, sizeof(packet));
    udp.endPacket();

    unsigned long start = millis();
    while (millis() - start < 600) {
        int len = udp.parsePacket();
        if (len > 56) {
            uint8_t resp[384];
            int readLen = udp.read(resp, sizeof(resp));
            if (readLen > 56) {
                uint8_t numNames = resp[56];
                int pos = 57;
                String fallback = "";
                for (int i = 0; i < numNames; i++) {
                    if (pos + 18 <= readLen) {
                        uint8_t nameType = resp[pos + 15];
                        uint8_t flagsHigh = resp[pos + 16];
                        bool isGroup = (flagsHigh & 0x80) != 0;

                        char nameBuf[16];
                        memcpy(nameBuf, &resp[pos], 15);
                        nameBuf[15] = '\0';
                        String nameStr = String(nameBuf);
                        nameStr.trim();

                        if (nameStr.length() > 0 && !isGroup) {
                            if (nameType == 0x00 || nameType == 0x20) {
                                udp.stop();
                                return nameStr;
                            } else if (fallback.length() == 0) {
                                fallback = nameStr;
                            }
                        }
                        pos += 18;
                    }
                }
                if (fallback.length() > 0) {
                    udp.stop();
                    return fallback;
                }
            }
        }
        delay(5);
    }

    udp.stop();
    return "";
}

int DeviceManager::_findDeviceIndex(const String& mac) const {
    for (size_t i = 0; i < _deviceCount; i++) {
        if (strcasecmp(_devices[i].mac, mac.c_str()) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void DeviceManager::updateDevice(const String& mac, const String& ip, const String& hostname,
                                const String& band, int8_t rssi, bool online,
                                uint64_t dl, uint64_t ul) {
    if (mac.length() == 0) return;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);

    int idx = _findDeviceIndex(mac);
    if (idx >= 0) {
        if (ip.length() > 0) strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
        if (hostname.length() > 0 && strcasecmp(hostname.c_str(), "Unknown") != 0) {
            strncpy(_devices[idx].hostname, hostname.c_str(), sizeof(_devices[idx].hostname) - 1);
        }
        if (band.length() > 0) strncpy(_devices[idx].band, band.c_str(), sizeof(_devices[idx].band) - 1);
        _devices[idx].rssi   = rssi;
        _devices[idx].online = online;
        if (dl > 0) _devices[idx].dlBytes = dl;
        if (ul > 0) _devices[idx].ulBytes = ul;
        _devices[idx].lastSeen = millis();
    } else if (_deviceCount < MAX_TRACKED_DEVICES) {
        idx = (int)_deviceCount++;
        strncpy(_devices[idx].mac, mac.c_str(), sizeof(_devices[idx].mac) - 1);
        strncpy(_devices[idx].ip, ip.c_str(), sizeof(_devices[idx].ip) - 1);
        strncpy(_devices[idx].hostname, hostname.length() > 0 ? hostname.c_str() : "Unknown",
                sizeof(_devices[idx].hostname) - 1);
        strncpy(_devices[idx].band, band.length() > 0 ? band.c_str() : "2.4G", sizeof(_devices[idx].band) - 1);
        _devices[idx].rssi         = rssi;
        _devices[idx].online       = online;
        _devices[idx].blocked      = false;
        _devices[idx].dlBytes      = dl;
        _devices[idx].ulBytes      = ul;
        _devices[idx].lastSeen     = millis();
        _devices[idx].netbiosTries = 0;
    }

    if (_mutex) xSemaphoreGive(_mutex);
}

bool DeviceManager::setBlocked(const String& mac, bool blocked) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    if (idx >= 0) {
        _devices[idx].blocked = blocked;
    } else if (_deviceCount < MAX_TRACKED_DEVICES) {
        idx = (int)_deviceCount++;
        memset(&_devices[idx], 0, sizeof(ClientDevice));
        strncpy(_devices[idx].mac, mac.c_str(), sizeof(_devices[idx].mac) - 1);
        _devices[idx].blocked = blocked;
    }
    if (_mutex) xSemaphoreGive(_mutex);

    _saveBlockedMacs();
    return true;
}

bool DeviceManager::isBlocked(const String& mac) const {
    if (scheduler.hasActiveWaiver(mac)) return false; // Temporary waiver bypasses block

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = _findDeviceIndex(mac);
    bool b = (idx >= 0) ? _devices[idx].blocked : false;
    if (_mutex) xSemaphoreGive(_mutex);
    return b;
}

void DeviceManager::_saveBlockedMacs() {
    Preferences prefs;
    prefs.begin("microrouter", false);
    String list = "";
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        if (_devices[i].blocked) {
            if (list.length() > 0) list += ",";
            list += _devices[i].mac;
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);
    prefs.putString("blk_macs", list);
    prefs.end();
}

void DeviceManager::_loadBlockedMacs() {
    Preferences prefs;
    prefs.begin("microrouter", true);
    String list = prefs.getString("blk_macs", "");
    prefs.end();

    if (list.length() > 0) {
        int start = 0;
        int end = list.indexOf(',');
        while (end >= 0) {
            String mac = list.substring(start, end);
            mac.trim();
            if (mac.length() > 0) setBlocked(mac, true);
            start = end + 1;
            end = list.indexOf(',', start);
        }
        String mac = list.substring(start);
        mac.trim();
        if (mac.length() > 0) setBlocked(mac, true);
    }
}

String DeviceManager::getDevicesJson() const {
    JsonDocument doc;
    JsonArray connArr = doc["connected"].to<JsonArray>();
    JsonArray allArr  = doc["all"].to<JsonArray>();

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 0; i < _deviceCount; i++) {
        JsonObject d = allArr.add<JsonObject>();
        d["mac"]      = _devices[i].mac;
        d["ip"]       = _devices[i].ip;
        d["hostname"] = _devices[i].hostname;
        d["band"]     = _devices[i].band;
        d["rssi"]     = _devices[i].rssi;
        d["online"]   = _devices[i].online;
        d["blocked"]  = _devices[i].blocked;
        d["waiver"]   = scheduler.hasActiveWaiver(_devices[i].mac);
        d["dlBytes"]  = _devices[i].dlBytes;
        d["ulBytes"]  = _devices[i].ulBytes;

        if (_devices[i].online) {
            connArr.add(d);
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);

    doc["connectedCount"] = connArr.size();
    doc["totalCount"]     = allArr.size();

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

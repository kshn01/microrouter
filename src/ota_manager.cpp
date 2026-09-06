#include "ota_manager.h"
#include <ArduinoOTA.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>
#include <ArduinoJson.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  OTAManager Implementation                                  ║
// ╚══════════════════════════════════════════════════════════════╝

void OTAManager::begin(AsyncWebServer* server) {
    Serial.println("[OTA] Initializing...");

    _bootTime = millis();
    _incrementBootCount();

    // Set up both OTA methods
    _setupArduinoOTA();
    _setupElegantOTA(server);

    Serial.printf("[OTA] Firmware: %s | Partition: %s | Boot #%u\n",
                  FIRMWARE_VERSION, getPartitionLabel().c_str(), _bootCount);
}

void OTAManager::loop() {
    // Handle ArduinoOTA (PlatformIO wireless upload)
    ArduinoOTA.handle();

    // Handle ElegantOTA (browser-based upload)
    ElegantOTA.loop();

    // Boot validation — mark firmware as valid after stable running period
    if (!_firmwareValidated && (millis() - _bootTime) > BOOT_VALIDATION_DELAY_MS) {
        _validateBoot();
    }
}

String OTAManager::getPartitionLabel() const {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        return String(running->label);
    }
    return "unknown";
}

String OTAManager::getStatusJson() const {
    JsonDocument doc;

    doc["version"]          = FIRMWARE_VERSION;
    doc["name"]             = FIRMWARE_NAME;
    doc["partition"]        = getPartitionLabel();
    doc["bootCount"]        = _bootCount;
    doc["firmwareValidated"] = _firmwareValidated;
    doc["bootTimeMs"]       = _bootTime;
    doc["validationDelayMs"] = BOOT_VALIDATION_DELAY_MS;

    // Partition info
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        doc["partitionSize"]  = running->size;
        doc["partitionAddr"]  = String(running->address, HEX);
    }

    // Next OTA partition
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (next) {
        doc["nextPartition"] = next->label;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

// ── Private Methods ──────────────────────────────────────────────

void OTAManager::_setupArduinoOTA() {
    ArduinoOTA.setHostname(DEVICE_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.printf("[OTA-Arduino] Updating %s...\n", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA-Arduino] Update complete! Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA-Arduino] Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* errMsg;
        switch (error) {
            case OTA_AUTH_ERROR:    errMsg = "Auth Failed";     break;
            case OTA_BEGIN_ERROR:   errMsg = "Begin Failed";    break;
            case OTA_CONNECT_ERROR: errMsg = "Connect Failed";  break;
            case OTA_RECEIVE_ERROR: errMsg = "Receive Failed";  break;
            case OTA_END_ERROR:     errMsg = "End Failed";      break;
            default:                errMsg = "Unknown Error";    break;
        }
        Serial.printf("[OTA-Arduino] Error: %s\n", errMsg);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] ArduinoOTA ready (for PlatformIO wireless upload)");
}

void OTAManager::_setupElegantOTA(AsyncWebServer* server) {
    // ElegantOTA provides a beautiful web UI at /update
    ElegantOTA.begin(server, OTA_USERNAME, OTA_PASSWORD);

    ElegantOTA.onStart([]() {
        Serial.println("[OTA-Web] Browser upload started...");
    });

    ElegantOTA.onProgress([](size_t current, size_t final_size) {
        if (final_size > 0) {
            Serial.printf("[OTA-Web] Progress: %u%%\r", (unsigned int)((current * 100) / final_size));
        }
    });

    ElegantOTA.onEnd([](bool success) {
        if (success) {
            Serial.println("\n[OTA-Web] Update successful! Rebooting...");
        } else {
            Serial.println("\n[OTA-Web] Update FAILED!");
        }
    });

    Serial.println("[OTA] ElegantOTA ready at /update (browser-based upload)");
}

void OTAManager::_validateBoot() {
    // Mark the current firmware as valid — prevents automatic rollback
    esp_ota_mark_app_valid_cancel_rollback();
    _firmwareValidated = true;
    Serial.printf("[OTA] Firmware validated after %lu ms of stable operation.\n",
                  millis() - _bootTime);
}

void OTAManager::_incrementBootCount() {
    _prefs.begin(NVS_NAMESPACE, false);
    _bootCount = _prefs.getUInt(NVS_KEY_BOOT_COUNT, 0) + 1;
    _prefs.putUInt(NVS_KEY_BOOT_COUNT, _bootCount);
    _prefs.end();
}

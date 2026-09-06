#pragma once

#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  OTAManager — Dual-Bank OTA with Rollback Protection       ║
// ╚══════════════════════════════════════════════════════════════╝

class OTAManager {
public:
    /// Initialize OTA subsystems
    /// @param server  Pointer to the AsyncWebServer (for ElegantOTA web UI)
    void begin(AsyncWebServer* server);

    /// Call in loop() — handles ArduinoOTA + ElegantOTA + boot validation
    void loop();

    /// Get firmware version string
    const char* getVersion() const { return FIRMWARE_VERSION; }

    /// Get the currently running partition label
    String getPartitionLabel() const;

    /// Get boot count from NVS
    uint32_t getBootCount() const { return _bootCount; }

    /// Check if firmware has been validated this boot
    bool isFirmwareValidated() const { return _firmwareValidated; }

    /// Get OTA status as JSON
    String getStatusJson() const;

private:
    void _setupArduinoOTA();
    void _setupElegantOTA(AsyncWebServer* server);
    void _validateBoot();
    void _incrementBootCount();

    Preferences   _prefs;
    bool          _firmwareValidated = false;
    uint32_t      _bootCount = 0;
    unsigned long _bootTime = 0;
};

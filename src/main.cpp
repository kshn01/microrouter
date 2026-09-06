// ╔══════════════════════════════════════════════════════════════╗
// ║  MicroRouter — ESP32-S3 Advanced WiFi Router Portal         ║
// ║  Main Entry Point                                           ║
// ╚══════════════════════════════════════════════════════════════╝

#include <Arduino.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include "config.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "web_server.h"

// ── Global Instances ─────────────────────────────────────────────
WiFiManager wifiManager;
OTAManager  otaManager;
WebServer   webServer;

// ══════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════
void setup() {
    // ── Serial ───────────────────────────────────────────────────
    Serial.begin(115200);
    delay(1000);  // Wait for serial monitor

    Serial.println();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║        MicroRouter — Booting...              ║");
    Serial.printf( "║  Firmware: %-34s ║\n", FIRMWARE_VERSION);
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();

    // ── LittleFS ─────────────────────────────────────────────────
    Serial.println("[Boot] Mounting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("[Boot] ERROR: LittleFS mount failed!");
        // Continue without filesystem — API will still work
    } else {
        Serial.printf("[Boot] LittleFS: %u KB used / %u KB total\n",
                      LittleFS.usedBytes() / 1024,
                      LittleFS.totalBytes() / 1024);
    }

    // ── WiFi ─────────────────────────────────────────────────────
    Serial.println("[Boot] Starting WiFi...");
    wifiManager.begin();

    // ── mDNS ─────────────────────────────────────────────────────
    if (wifiManager.isConnected()) {
        if (MDNS.begin(DEVICE_HOSTNAME)) {
            MDNS.addService("http", "tcp", WEB_SERVER_PORT);
            Serial.printf("[Boot] mDNS: http://%s.local\n", DEVICE_HOSTNAME);
        } else {
            Serial.println("[Boot] WARNING: mDNS failed to start");
        }
    }

    // ── Web Server ───────────────────────────────────────────────
    Serial.println("[Boot] Starting web server...");
    webServer.begin(wifiManager, otaManager);

    // ── OTA ──────────────────────────────────────────────────────
    Serial.println("[Boot] Starting OTA...");
    otaManager.begin(webServer.getServer());

    // ── Boot Complete ────────────────────────────────────────────
    Serial.println();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║        MicroRouter — Ready!                  ║");
    Serial.printf( "║  IP:    %-38s║\n", wifiManager.getIP().c_str());
    Serial.printf( "║  Mode:  %-38s║\n",
                   wifiManager.isConnected() ? "Station (STA)" : "Access Point (AP)");
    Serial.printf( "║  Portal: http://%-30s║\n",
                   wifiManager.isConnected()
                       ? (String(DEVICE_HOSTNAME) + ".local").c_str()
                       : wifiManager.getIP().c_str());
    Serial.printf( "║  OTA:   http://%-30s║\n",
                   (wifiManager.getIP() + "/update").c_str());
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();
}

// ══════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════
void loop() {
    wifiManager.loop();     // Handle WiFi reconnection & DNS
    otaManager.loop();      // Handle OTA updates & boot validation
    webServer.loop();       // Handle WebSocket broadcasts & cleanup
}

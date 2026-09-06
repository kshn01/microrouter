#pragma once

// ── Target Platform Definitions for IDE Linters (clang / IntelliSense) ──
#ifndef ESP32
#define ESP32 1
#endif
#ifndef ARDUINO_ARCH_ESP32
#define ARDUINO_ARCH_ESP32 1
#endif

// ╔══════════════════════════════════════════════════════════════╗
// ║  MicroRouter — Global Configuration                         ║
// ╚══════════════════════════════════════════════════════════════╝

// ── Firmware Identity ────────────────────────────────────────────
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif
#define FIRMWARE_NAME       "MicroRouter"
#define DEVICE_HOSTNAME     "microrouter"

// ── WiFi — Station Mode ─────────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS     15000   // 15s to connect before falling back to AP
#define WIFI_RECONNECT_INTERVAL_MS  30000   // Retry every 30s if connection lost

// ── WiFi — Access Point (Setup Mode) ────────────────────────────
#define WIFI_AP_SSID                "MicroRouter-Setup"
#define WIFI_AP_PASSWORD            "setup1234"
#define WIFI_AP_CHANNEL             1
#define WIFI_AP_MAX_CONNECTIONS     4
#define WIFI_AP_IP                  IPAddress(192, 168, 4, 1)

// ── OTA Updates ──────────────────────────────────────────────────
#define OTA_USERNAME                "admin"
#define OTA_PASSWORD                "microrouter"
#define BOOT_VALIDATION_DELAY_MS    30000   // 30s — mark firmware valid after this

// ── Web Server ───────────────────────────────────────────────────
#define WEB_SERVER_PORT             80
#define WEBSOCKET_PATH              "/ws"
#define WS_MAX_CLIENTS              4
#define WS_BROADCAST_INTERVAL_MS    2000    // Push stats every 2s

// ── Router Gateway ───────────────────────────────────────────────
#define ROUTER_GATEWAY_IP           "192.168.1.1"

// ── NVS Namespace Keys ───────────────────────────────────────────
#define NVS_NAMESPACE               "microrouter"
#define NVS_KEY_SSID                "wifi_ssid"
#define NVS_KEY_PASSWORD            "wifi_pass"
#define NVS_KEY_BOOT_COUNT          "boot_count"
#define NVS_KEY_OTA_STATE           "ota_state"

#pragma once

#include "config.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include "wifi_manager.h"
#include "ota_manager.h"

// ╔══════════════════════════════════════════════════════════════╗
// ║  WebServer — HTTP API + WebSocket + Static File Server      ║
// ╚══════════════════════════════════════════════════════════════╝

class WebServer {
public:
    /// Initialize and start the web server
    /// @param wifiMgr   Reference to WiFi manager (for WiFi API endpoints)
    /// @param otaMgr    Reference to OTA manager (for system info endpoints)
    void begin(WiFiManager& wifiMgr, OTAManager& otaMgr);

    /// Get the underlying AsyncWebServer (needed by OTAManager)
    AsyncWebServer* getServer() { return &_server; }

    /// Broadcast a message to all connected WebSocket clients
    void broadcastStats();

    /// Call in loop() — handles WebSocket cleanup + periodic broadcasts
    void loop();

private:
    // ── Route Setup ──────────────────────────────────────────────
    void _setupStaticFiles();
    void _setupApiRoutes();
    void _setupWebSocket();

    // ── API Handlers ─────────────────────────────────────────────
    void _handleSystemInfo(AsyncWebServerRequest* request);
    void _handleSystemHealth(AsyncWebServerRequest* request);
    void _handleSystemRestart(AsyncWebServerRequest* request);
    void _handleWiFiStatus(AsyncWebServerRequest* request);
    void _handleWiFiScan(AsyncWebServerRequest* request);
    void _handleWiFiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleWiFiDisconnect(AsyncWebServerRequest* request);
    void _handleNotFound(AsyncWebServerRequest* request);

    // ── DNS Shield Handlers ──────────────────────────────────────
    void _handleDnsGet(AsyncWebServerRequest* request);
    void _handleDnsSet(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleDnsQueries(AsyncWebServerRequest* request);
    void _handleDnsQueriesClear(AsyncWebServerRequest* request);

    // ── Device Management Handlers ───────────────────────────────
    void _handleDevices(AsyncWebServerRequest* request);
    void _handleDeviceBlock(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleDeviceAllow(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleDeviceWaiver(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // ── Curfew & Quota Handlers ──────────────────────────────────
    void _handleCurfewGet(AsyncWebServerRequest* request);
    void _handleCurfewSet(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleQuotaSet(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // ── Router Gateway & Analytics Handlers ──────────────────────
    void _handleRouterReboot(AsyncWebServerRequest* request);
    void _handleRouterWifiToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleRouterSsidToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleRouterDnsGet(AsyncWebServerRequest* request);
    void _handleRouterDnsSet(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleGuestAnalytics(AsyncWebServerRequest* request);
    void _handleGuestAnalyticsDelete(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void _handleGuestQuotaClear(AsyncWebServerRequest* request);
    void _handleLastLog(AsyncWebServerRequest* request);
    void _handleSession(AsyncWebServerRequest* request);

    // ── WebSocket ────────────────────────────────────────────────
    void _onWsEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
    String _buildStatsJson();

    AsyncWebServer  _server{WEB_SERVER_PORT};
    AsyncWebSocket  _ws{WEBSOCKET_PATH};
    WiFiManager*    _wifiMgr = nullptr;
    OTAManager*     _otaMgr  = nullptr;
    unsigned long   _lastBroadcast = 0;

    bool            _rebootPending = false;
    unsigned long   _rebootAt = 0;
    bool            _connectPending = false;
    String          _pendingSsid;
    String          _pendingPass;
};

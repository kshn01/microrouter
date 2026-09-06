#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "router_client.h"
#include "crypto_helpers.h"

// ╔══════════════════════════════════════════════════════════════╗
// ║  ZteRouterClient — ZTE GPON Router (F670L / F680) Client     ║
// ║  Implements zero-copy streaming XML parser & RSA/SHA256 Auth ║
// ╚══════════════════════════════════════════════════════════════╝

class ZteRouterClient : public RouterClient {
public:
    ZteRouterClient();
    ~ZteRouterClient() override;

    bool begin() override;
    void loop() override;

    bool reboot() override;
    bool toggleRadio(bool enable) override;
    bool toggleSSID(int ssidIdx, bool enable) override;
    bool syncDns(const String& primaryDns, const String& secondaryDns) override;
    String getGatewayType() const override { return "ZTE GPON (F670L)"; }
    bool isLoggedIn() const override { return _loggedIn; }
    void getStats(int& cpu, int& mem, String& uptime) const override;
    String getLastLog() const override;

    // Direct ZTE controls
    bool login();
    bool relogin();
    void fetchRealTimeStats();
    void fetchRouterHealth();
    String routerGET(const String& path);

private:
    String _getRawInitialSID();
    String _extractTokenFromStream(WiFiClient* stream, unsigned long maxWaitMs = 8000);
    String _xmlExtract(const String& xml, const String& tag);

    SemaphoreHandle_t _httpMutex = nullptr;
    String            _routerIp;
    String            _routerUser;
    String            _routerPass;
    String            _sidCookie;
    String            _sessToken;
    bool              _loggedIn = false;
    unsigned long     _lastLoginTime = 0;
    unsigned long     _lastPollTime = 0;
    unsigned long     _lastHealthTime = 0;

    int               _routerCpu = 0;
    int               _routerMem = 0;
    String            _routerUptime = "0m";
    String            _lastLog = "ZTE Gateway Initialized";
};

extern ZteRouterClient zteClient;

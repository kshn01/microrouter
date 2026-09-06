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

    // Direct Router DNS Profile Management, DHCP DNS & Local Domain Registration
    String getDnsContextToken();
    String getLanIpv4ContextToken();
    bool fetchRouterDnsSettings(String& primaryDns, String& secondaryDns);
    bool fetchRouterDhcpDnsSettings(String& primaryDns, String& secondaryDns, int& dnsSource);
    bool applyRouterDns(const String& primaryDns, const String& secondaryDns);
    bool applyRouterDhcpDns(const String& primaryDns, const String& secondaryDns);
    bool registerRouterLocalDomain(const String& hostname, const String& ip);
    bool syncRouterDnsProfile(const String& profileKey, const String& customPrimary = "", const String& customSecondary = "", bool highAvailability = true);
    void syncRouterDnsAtBoot();

    bool isDnsSynced() const { return _dnsSynced; }
    String getRouterDnsPrimary() const { return _routerDnsPrimary; }
    String getRouterDnsSecondary() const { return _routerDnsSecondary; }
    bool isHaMode() const { return _haMode; }

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

    bool              _dnsSynced = false;
    bool              _dnsBootSynced = false;
    bool              _haMode = true;
    String            _routerDnsPrimary = "192.168.1.7";
    String            _routerDnsSecondary = "1.1.1.1";
    String            _lastSyncedIp = "";
};

extern ZteRouterClient zteClient;

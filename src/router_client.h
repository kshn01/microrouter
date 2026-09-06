#pragma once

#include <Arduino.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  RouterClient — Gateway Integration Interface               ║
// ║  Supports Generic Gateway mode & ZTE GPON Router mode        ║
// ╚══════════════════════════════════════════════════════════════╝

class RouterClient {
public:
    virtual ~RouterClient() = default;

    virtual bool begin() = 0;
    virtual void loop() = 0;

    virtual bool reboot() = 0;
    virtual bool toggleRadio(bool enable) = 0;
    virtual bool toggleSSID(int ssidIdx, bool enable) { return false; }
    virtual bool syncDns(const String& primaryDns, const String& secondaryDns) = 0;
    virtual String getGatewayType() const = 0;
    virtual bool isLoggedIn() const { return false; }
    virtual void getStats(int& cpu, int& mem, String& uptime) const { cpu = 0; mem = 0; uptime = "0m"; }
    virtual String getLastLog() const { return "Gateway ready"; }
};

class GenericRouterClient : public RouterClient {
public:
    bool begin() override {
        Serial.println("[RouterClient] Initialized in Generic Gateway mode.");
        return true;
    }

    void loop() override {}

    bool reboot() override {
        Serial.println("[RouterClient] Reboot requested (Generic Gateway).");
        return false; // Cannot remote reboot generic router without vendor credentials
    }

    bool toggleRadio(bool enable) override {
        Serial.printf("[RouterClient] Toggle radio: %d\n", enable);
        return false;
    }

    bool toggleSSID(int ssidIdx, bool enable) override {
        Serial.printf("[RouterClient] Toggle SSID %d: %d\n", ssidIdx, enable);
        return false;
    }

    bool syncDns(const String& primaryDns, const String& secondaryDns) override {
        Serial.printf("[RouterClient] Sync DNS: %s, %s\n", primaryDns.c_str(), secondaryDns.c_str());
        return true;
    }

    String getGatewayType() const override {
        return "Generic Gateway";
    }
};

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
    virtual bool syncDns(const String& primaryDns, const String& secondaryDns) = 0;
    virtual String getGatewayType() const = 0;
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

    bool syncDns(const String& primaryDns, const String& secondaryDns) override {
        Serial.printf("[RouterClient] Sync DNS: %s, %s\n", primaryDns.c_str(), secondaryDns.c_str());
        return true;
    }

    String getGatewayType() const override {
        return "Generic Gateway";
    }
};

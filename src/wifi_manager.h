#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  WiFiManager — STA/AP Connection Management                 ║
// ╚══════════════════════════════════════════════════════════════╝

/// Connection state machine
enum class NetState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED_STA,
    AP_ACTIVE
};

class WiFiManager {
public:
    /// Initialize WiFi — tries saved creds, falls back to AP mode
    void begin();

    /// Connect to a specific network (saves credentials on success)
    bool connect(const String& ssid, const String& password);

    /// Start Access Point for initial setup
    void startAP();

    /// Forget stored credentials
    void clearCredentials();

    /// Returns true if connected in STA mode
    bool isConnected() const;

    /// Get current IP address as string
    String getIP() const;

    /// Get connected SSID
    String getSSID() const;

    /// Get signal strength (dBm)
    int8_t getRSSI() const;

    /// Get current connection state
    NetState getState() const { return _state; }

    /// Scan for available networks — returns JSON array
    String scanNetworks();

    /// Get full WiFi status — returns JSON object
    String getStatusJson() const;

    /// Call in loop() — handles reconnection & DNS (AP mode)
    void loop();

private:
    bool _connectToSaved();
    bool _tryConnect(const String& ssid, const String& password, uint32_t timeout_ms);
    void _saveCredentials(const String& ssid, const String& password);
    bool _hasSavedCredentials();

    Preferences   _prefs;
    DNSServer     _dnsServer;
    NetState      _state = NetState::DISCONNECTED;
    unsigned long _lastReconnectAttempt = 0;
    bool          _apDnsRunning = false;
    bool          _scanInProgress = false;
};

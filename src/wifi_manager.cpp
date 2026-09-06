#include "wifi_manager.h"
#include "config.h"
#include <ArduinoJson.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  WiFiManager Implementation                                 ║
// ╚══════════════════════════════════════════════════════════════╝

void WiFiManager::begin() {
    Serial.println("[WiFi] Initializing...");

    // Disconnect any previous connections
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);

    // Try to connect to saved network
    if (_hasSavedCredentials() && _connectToSaved()) {
        Serial.println("[WiFi] Connected to saved network!");
        return;
    }

#if defined(DEFAULT_WIFI_SSID) && defined(DEFAULT_WIFI_PASS)
    if (strlen(DEFAULT_WIFI_SSID) > 0) {
        Serial.printf("[WiFi] Trying default configured network: %s\n", DEFAULT_WIFI_SSID);
        if (_tryConnect(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, WIFI_CONNECT_TIMEOUT_MS)) {
            _saveCredentials(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
            Serial.println("[WiFi] Connected to default network and saved to NVS!");
            return;
        }
    }
#endif

    // No saved creds or connection failed — start AP for setup
    Serial.println("[WiFi] No saved network or connection failed. Starting AP...");
    startAP();
}

bool WiFiManager::connect(const String& ssid, const String& password) {
    Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());

    // If we're in AP mode, stop it first
    if (_state == NetState::AP_ACTIVE) {
        if (_apDnsRunning) {
            _dnsServer.stop();
            _apDnsRunning = false;
        }
        WiFi.softAPdisconnect(true);
    }

    WiFi.mode(WIFI_STA);

    if (_tryConnect(ssid, password, WIFI_CONNECT_TIMEOUT_MS)) {
        _saveCredentials(ssid, password);
        _state = NetState::CONNECTED_STA;
        Serial.printf("[WiFi] Connected! IP: %s\n", getIP().c_str());
        return true;
    }

    Serial.println("[WiFi] Connection failed. Reverting to AP mode.");
    startAP();
    return false;
}

void WiFiManager::startAP() {
    Serial.println("[WiFi] Starting Access Point...");

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS);

    // Start DNS server — redirect all domains to our IP (captive portal)
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _apDnsRunning = true;

    _state = NetState::AP_ACTIVE;
    Serial.printf("[WiFi] AP started: %s (password: %s)\n", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    Serial.printf("[WiFi] Portal at: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::clearCredentials() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_SSID);
    _prefs.remove(NVS_KEY_PASSWORD);
    _prefs.end();
    Serial.println("[WiFi] Credentials cleared.");
}

bool WiFiManager::isConnected() const {
    return _state == NetState::CONNECTED_STA && WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIP() const {
    if (_state == NetState::AP_ACTIVE) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

String WiFiManager::getSSID() const {
    if (_state == NetState::CONNECTED_STA) {
        return WiFi.SSID();
    }
    return WIFI_AP_SSID;
}

int8_t WiFiManager::getRSSI() const {
    if (_state == NetState::CONNECTED_STA) {
        return WiFi.RSSI();
    }
    return 0;
}

String WiFiManager::scanNetworks() {
    Serial.println("[WiFi] Scanning networks...");

    int n = WiFi.scanNetworks(false, false);

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"]     = WiFi.SSID(i);
            net["rssi"]     = WiFi.RSSI(i);
            net["channel"]  = WiFi.channel(i);
            net["secure"]   = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        doc["count"] = n;
        WiFi.scanDelete();
    } else {
        doc["count"] = 0;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String WiFiManager::getStatusJson() const {
    JsonDocument doc;

    const char* stateStr;
    switch (_state) {
        case NetState::DISCONNECTED:  stateStr = "disconnected"; break;
        case NetState::CONNECTING:    stateStr = "connecting";    break;
        case NetState::CONNECTED_STA: stateStr = "connected";    break;
        case NetState::AP_ACTIVE:     stateStr = "ap_mode";       break;
        default:                      stateStr = "unknown";       break;
    }

    doc["state"]    = stateStr;
    doc["ssid"]     = getSSID();
    doc["ip"]       = getIP();
    doc["rssi"]     = getRSSI();
    doc["mac"]      = WiFi.macAddress();
    doc["channel"]  = (_state == NetState::CONNECTED_STA) ? WiFi.channel() : WIFI_AP_CHANNEL;
    doc["gateway"]  = (_state == NetState::CONNECTED_STA) ? WiFi.gatewayIP().toString() : "";
    doc["subnet"]   = (_state == NetState::CONNECTED_STA) ? WiFi.subnetMask().toString() : "255.255.255.0";
    doc["dns"]      = (_state == NetState::CONNECTED_STA) ? WiFi.dnsIP().toString() : "";
    doc["ap_mode"]  = (_state == NetState::AP_ACTIVE);

    String output;
    serializeJson(doc, output);
    return output;
}

void WiFiManager::loop() {
    // Handle DNS requests in AP mode (captive portal redirect)
    if (_apDnsRunning) {
        _dnsServer.processNextRequest();
    }

    // Auto-reconnect in STA mode
    if (_state == NetState::CONNECTED_STA && WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > WIFI_RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            Serial.println("[WiFi] Connection lost. Attempting reconnect...");
            _state = NetState::CONNECTING;

            if (_connectToSaved()) {
                _state = NetState::CONNECTED_STA;
                Serial.printf("[WiFi] Reconnected! IP: %s\n", getIP().c_str());
            } else {
                _state = NetState::CONNECTED_STA;  // Keep trying next interval
                Serial.println("[WiFi] Reconnect failed. Will retry...");
            }
        }
    }
}

// ── Private Methods ──────────────────────────────────────────────

bool WiFiManager::_connectToSaved() {
    _prefs.begin(NVS_NAMESPACE, true);  // Read-only
    String ssid = _prefs.getString(NVS_KEY_SSID, "");
    String pass = _prefs.getString(NVS_KEY_PASSWORD, "");
    _prefs.end();

    if (ssid.isEmpty()) {
        return false;
    }

    Serial.printf("[WiFi] Trying saved network: %s\n", ssid.c_str());
    return _tryConnect(ssid, pass, WIFI_CONNECT_TIMEOUT_MS);
}

bool WiFiManager::_tryConnect(const String& ssid, const String& password, uint32_t timeout_ms) {
    _state = NetState::CONNECTING;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        _state = NetState::CONNECTED_STA;
        return true;
    }

    WiFi.disconnect();
    _state = NetState::DISCONNECTED;
    return false;
}

void WiFiManager::_saveCredentials(const String& ssid, const String& password) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString(NVS_KEY_SSID, ssid);
    _prefs.putString(NVS_KEY_PASSWORD, password);
    _prefs.end();
    Serial.println("[WiFi] Credentials saved to NVS.");
}

bool WiFiManager::_hasSavedCredentials() {
    _prefs.begin(NVS_NAMESPACE, true);
    bool has = _prefs.isKey(NVS_KEY_SSID);
    _prefs.end();
    return has;
}

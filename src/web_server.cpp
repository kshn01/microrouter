#include "web_server.h"
#include "dns_engine.h"
#include "scheduler.h"
#include "device_manager.h"
#include "router_client.h"
#include "zte_client.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_chip_info.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  WebServer Implementation                                   ║
// ╚══════════════════════════════════════════════════════════════╝

void WebServer::begin(WiFiManager& wifiMgr, OTAManager& otaMgr) {
    _wifiMgr = &wifiMgr;
    _otaMgr  = &otaMgr;

    Serial.println("[Web] Initializing server...");

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    _setupApiRoutes();
    _setupWebSocket();
    _setupStaticFiles();

    _server.begin();
    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
}

void WebServer::broadcastStats() {
    if (_ws.count() > 0) {
        String json = _buildStatsJson();
        _ws.textAll(json);
    }
}

void WebServer::loop() {
    // Check deferred reboot
    if (_rebootPending && millis() > _rebootAt) {
        Serial.println("[Web] Executing deferred system restart...");
        ESP.restart();
    }

    // Check deferred WiFi connection
    if (_connectPending) {
        _connectPending = false;
        Serial.printf("[Web] Executing deferred WiFi connection to: %s\n", _pendingSsid.c_str());
        _wifiMgr->connect(_pendingSsid, _pendingPass);
    }

    // Periodic WebSocket broadcast
    unsigned long now = millis();
    if (now - _lastBroadcast > WS_BROADCAST_INTERVAL_MS) {
        _lastBroadcast = now;
        broadcastStats();
    }

    // Clean up dead WebSocket connections
    _ws.cleanupClients(WS_MAX_CLIENTS);
}

// ── Static Files ─────────────────────────────────────────────────

void WebServer::_setupStaticFiles() {
    // Serve the SPA from LittleFS
    _server.serveStatic("/", LittleFS, "/")
           .setDefaultFile("index.html")
           .setCacheControl("max-age=86400");

    // Fallback: serve index.html for SPA routes (hash-based routing
    // doesn't need this, but just in case)
    _server.onNotFound([this](AsyncWebServerRequest* request) {
        _handleNotFound(request);
    });

    Serial.println("[Web] Static files configured from LittleFS");
}

// ── WebSocket ────────────────────────────────────────────────────

void WebServer::_setupWebSocket() {
    _ws.onEvent([this](AsyncWebSocket* ws, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        _onWsEvent(ws, client, type, arg, data, len);
    });

    _server.addHandler(&_ws);
    Serial.println("[Web] WebSocket configured at " WEBSOCKET_PATH);
}

void WebServer::_onWsEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client,
                           AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            // Send initial stats immediately
            client->text(_buildStatsJson());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA: {
            // Handle incoming commands from the frontend
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // Parse JSON command directly from data buffer safely
                JsonDocument doc;
                if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
                    const char* action = doc["action"] | "";
                    Serial.printf("[WS] Command from #%u: %s\n", client->id(), action);
                    if (strcmp(action, "restart") == 0) {
                        client->text("{\"type\":\"ack\",\"action\":\"restart\"}");
                        _rebootPending = true;
                        _rebootAt = millis() + 1000;
                    }
                }
            }
            break;
        }

        case WS_EVT_ERROR:
            Serial.printf("[WS] Error on client #%u\n", client->id());
            break;

        case WS_EVT_PONG:
            break;
    }
}

String WebServer::_buildStatsJson() {
    JsonDocument doc;

    doc["type"] = "stats";

    JsonObject data = doc["data"].to<JsonObject>();
    data["uptimeMs"]      = millis();
    data["freeHeap"]      = ESP.getFreeHeap();
    data["totalHeap"]     = ESP.getHeapSize();
    data["minFreeHeap"]   = ESP.getMinFreeHeap();
    data["heapPercent"]   = (int)(((float)ESP.getFreeHeap() / ESP.getHeapSize()) * 100);
    data["wifiConnected"] = _wifiMgr->isConnected();
    data["wifiRssi"]      = _wifiMgr->getRSSI();
    data["wifiSsid"]      = _wifiMgr->getSSID();
    data["wifiIp"]        = _wifiMgr->getIP();
    data["fwVersion"]     = FIRMWARE_VERSION;
    data["fwValidated"]   = _otaMgr->isFirmwareValidated();
    data["partition"]     = _otaMgr->getPartitionLabel();
    data["bootCount"]     = _otaMgr->getBootCount();
    data["wsClients"]     = _ws.count();

    // DNS Shield Telemetry
    DnsStatsSnapshot dnsSnap;
    dnsEngine.getStats(&dnsSnap);
    data["dnsTotal"]      = dnsSnap.totalQueries;
    data["dnsBlocked"]    = dnsSnap.queriesBlocked;
    data["dnsProfile"]    = dnsSnap.profileKey;
    data["curfewActive"]  = scheduler.isCurfewActive();
    data["connectedDevices"] = deviceManager.getConnectedCount();

    // PSRAM info (ESP32-S3 often has PSRAM)
    if (ESP.getPsramSize() > 0) {
        data["psramSize"]   = ESP.getPsramSize();
        data["psramFree"]   = ESP.getFreePsram();
    }

    String output;
    serializeJson(doc, output);
    return output;
}

// ── API Routes ───────────────────────────────────────────────────

void WebServer::_setupApiRoutes() {
    // ── System Endpoints ─────────────────────────────────────────
    _server.on("/api/system/info", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleSystemInfo(req); });

    _server.on("/api/system/health", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleSystemHealth(req); });

    _server.on("/api/system/restart", HTTP_POST,
        [this](AsyncWebServerRequest* req) { _handleSystemRestart(req); });

    // ── WiFi Endpoints ───────────────────────────────────────────
    _server.on("/api/wifi/status", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleWiFiStatus(req); });

    _server.on("/api/wifi/scan", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleWiFiScan(req); });

    _server.on("/api/wifi/disconnect", HTTP_POST,
        [this](AsyncWebServerRequest* req) { _handleWiFiDisconnect(req); });

    // WiFi connect — needs body parsing
    _server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* req) { /* handled in onBody */ },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            _handleWiFiConnect(req, data, len);
        });

    // ── DNS Shield Endpoints ─────────────────────────────────────
    _server.on("/api/dns/get", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleDnsGet(req); });

    _server.on("/api/dns/queries", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleDnsQueries(req); });

    _server.on("/api/dns/queries/clear", HTTP_POST,
        [this](AsyncWebServerRequest* req) { _handleDnsQueriesClear(req); });

    _server.on("/api/dns/set", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleDnsSet(req, data, len);
        });

    // ── Device Management Endpoints ──────────────────────────────
    _server.on("/api/devices", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleDevices(req); });

    _server.on("/api/device/block", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleDeviceBlock(req, data, len);
        });

    _server.on("/api/device/allow", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleDeviceAllow(req, data, len);
        });

    _server.on("/api/device/waiver", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleDeviceWaiver(req, data, len);
        });

    // ── Curfew & Quota Endpoints ─────────────────────────────────
    _server.on("/api/guest/limit/get", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCurfewGet(req); });

    _server.on("/api/guest/limit/set", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleCurfewSet(req, data, len);
        });

    _server.on("/api/guest/quota/set", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleQuotaSet(req, data, len);
        });

    // ── Gateway & Router Parity Endpoints ────────────────────────
    _server.on("/api/reboot", HTTP_POST,
        [this](AsyncWebServerRequest* req) { _handleRouterReboot(req); });

    _server.on("/api/wifi/toggle", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            _handleRouterWifiToggle(req, nullptr, 0);
        },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleRouterWifiToggle(req, data, len);
        });

    _server.on("/api/ssid/toggle", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            _handleRouterSsidToggle(req, nullptr, 0);
        },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleRouterSsidToggle(req, data, len);
        });

    _server.on("/api/router/dns/get", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleRouterDnsGet(req); });

    _server.on("/api/router/dns/set", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            _handleRouterDnsSet(req, nullptr, 0);
        },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleRouterDnsSet(req, data, len);
        });

    _server.on("/api/guest/analytics", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleGuestAnalytics(req); });

    _server.on("/api/guest/analytics/delete", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            _handleGuestAnalyticsDelete(req, nullptr, 0);
        },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleGuestAnalyticsDelete(req, data, len);
        });

    _server.on("/api/guest/quota/clear", HTTP_POST,
        [this](AsyncWebServerRequest* req) { _handleGuestQuotaClear(req); });

    _server.on("/api/lastlog", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleLastLog(req); });

    _server.on("/api/session", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleSession(req); });

    Serial.println("[Web] API routes configured");
}

// ── System API Handlers ──────────────────────────────────────────

void WebServer::_handleSystemInfo(AsyncWebServerRequest* request) {
    JsonDocument doc;

    // Chip info
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);

    doc["chipModel"]     = ESP.getChipModel();
    doc["chipRevision"]  = ESP.getChipRevision();
    doc["cpuCores"]      = chipInfo.cores;
    doc["cpuFreqMHz"]    = ESP.getCpuFreqMHz();
    doc["flashSizeMB"]   = ESP.getFlashChipSize() / (1024 * 1024);
    doc["flashSpeedMHz"] = ESP.getFlashChipSpeed() / 1000000;
    doc["sdkVersion"]    = ESP.getSdkVersion();
    doc["sketchSize"]    = ESP.getSketchSize();
    doc["sketchFree"]    = ESP.getFreeSketchSpace();
    doc["macAddress"]    = WiFi.macAddress();

    // PSRAM
    doc["psramSize"]     = ESP.getPsramSize();
    doc["psramFree"]     = ESP.getFreePsram();

    // LittleFS
    doc["fsTotal"]       = LittleFS.totalBytes();
    doc["fsUsed"]        = LittleFS.usedBytes();

    // OTA info
    doc["fwVersion"]     = FIRMWARE_VERSION;
    doc["fwName"]        = FIRMWARE_NAME;
    doc["partition"]     = _otaMgr->getPartitionLabel();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::_handleSystemHealth(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["uptimeMs"]      = millis();
    doc["uptimeSec"]     = millis() / 1000;
    doc["freeHeap"]      = ESP.getFreeHeap();
    doc["totalHeap"]     = ESP.getHeapSize();
    doc["minFreeHeap"]   = ESP.getMinFreeHeap();
    doc["heapPercent"]   = (int)(((float)ESP.getFreeHeap() / ESP.getHeapSize()) * 100);
    doc["bootCount"]     = _otaMgr->getBootCount();
    doc["fwValidated"]   = _otaMgr->isFirmwareValidated();
    doc["wsClients"]     = _ws.count();

    // Reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr;
    switch (reason) {
        case ESP_RST_POWERON:  reasonStr = "Power-on";     break;
        case ESP_RST_SW:       reasonStr = "Software";     break;
        case ESP_RST_PANIC:    reasonStr = "Panic/Crash";  break;
        case ESP_RST_INT_WDT:  reasonStr = "INT Watchdog"; break;
        case ESP_RST_TASK_WDT: reasonStr = "Task Watchdog";break;
        case ESP_RST_WDT:      reasonStr = "Watchdog";     break;
        case ESP_RST_DEEPSLEEP:reasonStr = "Deep Sleep";   break;
        case ESP_RST_BROWNOUT: reasonStr = "Brownout";     break;
        case ESP_RST_SDIO:     reasonStr = "SDIO";         break;
        default:               reasonStr = "Unknown";       break;
    }
    doc["resetReason"] = reasonStr;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::_handleSystemRestart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    _rebootPending = true;
    _rebootAt = millis() + 1000;
}

// ── WiFi API Handlers ────────────────────────────────────────────

void WebServer::_handleWiFiStatus(AsyncWebServerRequest* request) {
    request->send(200, "application/json", _wifiMgr->getStatusJson());
}

void WebServer::_handleWiFiScan(AsyncWebServerRequest* request) {
    request->send(200, "application/json", _wifiMgr->scanNetworks());
}

void WebServer::_handleWiFiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);

    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();

    if (ssid.isEmpty()) {
        request->send(400, "application/json",
                      "{\"error\":\"SSID is required\"}");
        return;
    }

    // Respond immediately to avoid blocking HTTP client
    request->send(200, "application/json",
                  "{\"status\":\"connecting\",\"ssid\":\"" + ssid + "\"}");

    // Queue connection in loop() so AsyncTCP event thread is never blocked
    _pendingSsid = ssid;
    _pendingPass = password;
    _connectPending = true;
}

void WebServer::_handleWiFiDisconnect(AsyncWebServerRequest* request) {
    _wifiMgr->clearCredentials();
    request->send(200, "application/json", "{\"status\":\"disconnected\"}");

    _rebootPending = true;
    _rebootAt = millis() + 1000;  // Restart in AP mode after response delivers
}

// ── DNS Shield Handlers ──────────────────────────────────────────

void WebServer::_handleDnsGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    DnsStatsSnapshot snap;
    dnsEngine.getStats(&snap);
    doc["profile"]        = snap.profileKey;
    doc["profileName"]    = snap.profileName;
    doc["primary"]        = snap.upstreamPrimary;
    doc["secondary"]      = snap.upstreamSecondary;
    doc["primaryIp"]      = snap.upstreamPrimary;
    doc["secondaryIp"]    = snap.upstreamSecondary;
    doc["totalQueries"]   = snap.totalQueries;
    doc["answered"]       = snap.queriesAnswered;
    doc["forwarded"]      = snap.queriesForwarded;
    doc["blocked"]        = snap.queriesBlocked;
    doc["blockedQueries"] = snap.queriesBlocked;
    doc["local"]          = snap.localInterceptCount;
    doc["avgLatencyMs"]   = snap.avgLatencyMs;

    DnsShieldRules rules;
    dnsEngine.getShieldRules(&rules);
    doc["blockMeta"]    = rules.blockMeta;
    doc["blockTiktok"]  = rules.blockTiktok;
    doc["customDomains"]= rules.customDomains;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleDnsSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    if (doc["profile"].is<const char*>()) {
        dnsEngine.setProfile(doc["profile"].as<String>());
    }
    if (doc["customPrimary"].is<const char*>()) {
        dnsEngine.setCustomUpstreams(doc["customPrimary"].as<String>(),
                                     doc["customSecondary"] | "0.0.0.0");
    }
    if (doc["blockMeta"].is<bool>() || doc["blockTiktok"].is<bool>() || doc["customDomains"].is<const char*>()) {
        DnsShieldRules cur;
        dnsEngine.getShieldRules(&cur);
        bool bm = doc["blockMeta"].is<bool>() ? doc["blockMeta"].as<bool>() : cur.blockMeta;
        bool bt = doc["blockTiktok"].is<bool>() ? doc["blockTiktok"].as<bool>() : cur.blockTiktok;
        String cd = doc["customDomains"].is<const char*>() ? doc["customDomains"].as<String>() : String(cur.customDomains);
        dnsEngine.setShieldRules(bm, bt, cd);
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebServer::_handleDnsQueries(AsyncWebServerRequest* request) {
    request->send(200, "application/json", dnsEngine.getRecentQueriesJson());
}

void WebServer::_handleDnsQueriesClear(AsyncWebServerRequest* request) {
    dnsEngine.clearQueryLog();
    request->send(200, "application/json", "{\"status\":\"cleared\"}");
}

// ── Device Management Handlers ───────────────────────────────────

void WebServer::_handleDevices(AsyncWebServerRequest* request) {
    request->send(200, "application/json", deviceManager.getDevicesJson());
}

void WebServer::_handleDeviceBlock(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        deviceManager.setBlocked(mac, true);
        request->send(200, "application/json", "{\"status\":\"blocked\",\"mac\":\"" + mac + "\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleDeviceAllow(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        deviceManager.setBlocked(mac, false);
        scheduler.revokeWaiver(mac);
        request->send(200, "application/json", "{\"status\":\"allowed\",\"mac\":\"" + mac + "\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleDeviceWaiver(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        uint32_t secs = doc["durationSecs"] | 1800; // default 30 mins
        scheduler.grantWaiver(mac, secs);
        request->send(200, "application/json",
                      "{\"status\":\"waiver_granted\",\"mac\":\"" + mac +
                      "\",\"durationSecs\":" + String(secs) + "}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

// ── Curfew & Quota Handlers ──────────────────────────────────────

void WebServer::_handleCurfewGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    CurfewSchedule cs;
    scheduler.getCurfew(&cs);
    doc["enabled"]   = cs.enabled;
    doc["startHour"] = cs.startHour;
    doc["startMin"]  = cs.startMin;
    doc["endHour"]   = cs.endHour;
    doc["endMin"]    = cs.endMin;
    doc["activeNow"] = scheduler.isCurfewActive();
    doc["timeStr"]   = scheduler.getFormattedTime();

    QuotaLimits q;
    scheduler.getQuotas(&q);
    doc["hourlyEnabled"]   = q.hourlyEnabled;
    doc["hourlyLimitMb"]   = (uint32_t)(q.hourlyLimitBytes / (1024 * 1024));
    doc["dailyEnabled"]    = q.dailyEnabled;
    doc["dailyLimitMb"]    = (uint32_t)(q.dailyLimitBytes / (1024 * 1024));
    doc["timeEnabled"]     = q.timeEnabled;
    doc["dailyActiveMins"] = q.dailyActiveSecs / 60;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleCurfewSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        bool en = doc["enabled"] | false;
        int sH = doc["startHour"] | 23;
        int sM = doc["startMin"] | 0;
        int eH = doc["endHour"] | 6;
        int eM = doc["endMin"] | 0;
        scheduler.setCurfew(en, sH, sM, eH, eM);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleQuotaSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        uint64_t hMb = doc["hourlyLimitMb"] | 500;
        uint64_t dMb = doc["dailyLimitMb"] | 2048;
        uint32_t tm  = (doc["dailyActiveMins"] | 60) * 60;
        scheduler.setQuotas(doc["hourlyEnabled"] | false, hMb * 1024 * 1024ULL,
                            doc["dailyEnabled"] | false, dMb * 1024 * 1024ULL,
                            doc["timeEnabled"] | false, tm);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

// ── Router Gateway Parity Handlers ───────────────────────────────

void WebServer::_handleRouterReboot(AsyncWebServerRequest* request) {
    bool ok = zteClient.reboot();
    if (!ok || request->hasParam("system")) {
        _rebootPending = true;
        _rebootAt = millis() + 1000;
        ok = true;
    }
    request->send(200, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + "}");
}

void WebServer::_handleRouterWifiToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    bool on = true;
    if (request->hasParam("on")) {
        on = (request->getParam("on")->value() == "1" || request->getParam("on")->value().equalsIgnoreCase("true"));
    } else if (request->hasParam("enable")) {
        on = (request->getParam("enable")->value() == "1" || request->getParam("enable")->value().equalsIgnoreCase("true"));
    } else if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            on = doc["on"] | (doc["enable"] | true);
        }
    }
    bool ok = zteClient.toggleRadio(on);
    request->send(200, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + ",\"state\":\"" + (on ? "1" : "0") + "\"}");
}

void WebServer::_handleRouterSsidToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    int ssidIdx = 1;
    bool enable = true;
    if (request->hasParam("ssidIdx")) {
        ssidIdx = request->getParam("ssidIdx")->value().toInt();
    }
    if (request->hasParam("enable")) {
        enable = (request->getParam("enable")->value() == "1" || request->getParam("enable")->value().equalsIgnoreCase("true"));
    } else if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            ssidIdx = doc["ssidIdx"] | 1;
            enable = doc["enable"] | true;
        }
    }
    bool ok = zteClient.toggleSSID(ssidIdx, enable);
    request->send(200, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + "}");
}

void WebServer::_handleRouterDnsGet(AsyncWebServerRequest* request) {
    DnsStatsSnapshot stats;
    dnsEngine.getStats(&stats);

    JsonDocument doc;
    doc["ok"] = true;
    doc["profile"] = stats.profileKey;
    doc["primary"] = "1.1.1.1";
    doc["secondary"] = "1.0.0.1";
    doc["dhcpPrimary"] = _wifiMgr->getIP();
    doc["dhcpSecondary"] = "1.1.1.1";
    doc["hybridDns"] = true;
    doc["routerSynced"] = zteClient.isLoggedIn();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleRouterDnsSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    String profile = "ultra_fast";
    String primary = "1.1.1.1";
    String secondary = "1.0.0.1";

    if (request->hasParam("profile")) {
        profile = request->getParam("profile")->value();
    }
    if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (doc["profile"].is<const char*>()) profile = doc["profile"].as<String>();
            if (doc["primary"].is<const char*>()) primary = doc["primary"].as<String>();
            if (doc["secondary"].is<const char*>()) secondary = doc["secondary"].as<String>();
        }
    }

    dnsEngine.setProfile(profile);
    bool ok = zteClient.syncDns(primary, secondary);

    JsonDocument resp;
    resp["ok"] = true;
    resp["profile"] = profile;
    resp["primary"] = primary;
    resp["secondary"] = secondary;
    resp["routerSynced"] = ok;

    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleGuestAnalytics(AsyncWebServerRequest* request) {
    String json = deviceManager.getGuestAnalyticsJson();
    request->send(200, "application/json", json);
}

void WebServer::_handleGuestAnalyticsDelete(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    String mac = "";
    if (request->hasParam("mac")) {
        mac = request->getParam("mac")->value();
    } else if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            mac = doc["mac"] | "";
        }
    }
    bool ok = false;
    if (mac.length() > 0) {
        ok = deviceManager.deleteGuestAnalyticsRecord(mac);
    }
    request->send(200, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + "}");
}

void WebServer::_handleGuestQuotaClear(AsyncWebServerRequest* request) {
    deviceManager.clearGuestUsage();
    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::_handleLastLog(AsyncWebServerRequest* request) {
    request->send(200, "text/plain", zteClient.getLastLog());
}

void WebServer::_handleSession(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["sid"] = "active";
    doc["token"] = "session_token";
    doc["wifi"] = "1";
    doc["guest"] = true;
    doc["guestSsid"] = "MicroRouter-Guest";
    doc["ip"] = _wifiMgr->getIP();
    doc["loggedIn"] = zteClient.isLoggedIn();
    doc["gatewayType"] = zteClient.getGatewayType();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// ── 404 Handler ──────────────────────────────────────────────────

void WebServer::_handleNotFound(AsyncWebServerRequest* request) {
    if (request->method() == HTTP_OPTIONS) {
        request->send(200);
        return;
    }

    // For API routes — return JSON 404
    if (request->url().startsWith("/api/")) {
        request->send(404, "application/json",
                      "{\"error\":\"Not found\",\"path\":\"" + request->url() + "\"}");
        return;
    }

    // For all other routes — serve index.html (SPA fallback)
    request->send(LittleFS, "/index.html", "text/html");
}

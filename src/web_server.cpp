#include "web_server.h"
#include "dns_engine.h"
#include "scheduler.h"
#include "device_manager.h"
#include "router_client.h"
#include "zte_client.h"
#include "quota_notice_view.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
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
    // Intercept root route for restricted clients to display quota alert sheet
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        String clientIp = req->client() ? req->client()->remoteIP().toString() : "";
        if (clientIp.length() > 0 && deviceManager.isClientRestricted(clientIp)) {
            req->redirect("http://" + _wifiMgr->getIP() + "/quota-notice");
            return;
        }
        req->send(LittleFS, "/index.html", "text/html");
    });

    // Serve the SPA from LittleFS (no-cache ensures new deployments load immediately)
    _server.serveStatic("/", LittleFS, "/")
           .setDefaultFile("index.html")
           .setCacheControl("no-cache");

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
    data["wifiMac"]       = WiFi.macAddress();
    data["wifiChannel"]   = (_wifiMgr->isConnected()) ? WiFi.channel() : WIFI_AP_CHANNEL;
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

    _server.on("/api/device/parental", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleDeviceParental(req, data, len);
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

    // ── Captive Portal & Quota Notice Endpoints ──────────────────
    _server.on("/hotspot-detect.html", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/generate_204", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/gen_204", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/connecttest.txt", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/ncsi.txt", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/canonical.html", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/success.txt", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleCaptiveProbe(req); });

    _server.on("/quota-notice", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleQuotaNotice(req); });

    _server.on("/api/device/request-waiver", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            _handleRequestWaiver(req, nullptr, 0);
        },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            _handleRequestWaiver(req, data, len);
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

    _server.on("/api/router/dns/verify", HTTP_GET,
        [this](AsyncWebServerRequest* req) { _handleRouterDnsVerify(req); });

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

bool WebServer::_checkRateLimit(AsyncWebServerRequest* request) {
    if (!request || !request->client()) return true;
    uint32_t ip32 = (uint32_t)request->client()->remoteIP();
    if (!_rateLimiter.allow(ip32, millis())) {
        request->send(429, "application/json", "{\"status\":\"error\",\"message\":\"Rate limit exceeded\"}");
        return false;
    }
    return true;
}

// ── System API Handlers ──────────────────────────────────────────

void WebServer::_handleSystemInfo(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
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
    doc["fsTotalBytes"]  = LittleFS.totalBytes();
    doc["fsUsedBytes"]   = LittleFS.usedBytes();
    // Legacy aliases for older frontend builds
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
    if (!_checkRateLimit(request)) return;
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
    if (!_checkRateLimit(request)) return;
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    _rebootPending = true;
    _rebootAt = millis() + 1000;
}

// ── WiFi API Handlers ────────────────────────────────────────────

void WebServer::_handleWiFiStatus(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    request->send(200, "application/json", _wifiMgr->getStatusJson());
}

void WebServer::_handleWiFiScan(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    request->send(200, "application/json", _wifiMgr->scanNetworks());
}

void WebServer::_handleWiFiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
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
    if (!_checkRateLimit(request)) return;
    _wifiMgr->clearCredentials();
    request->send(200, "application/json", "{\"status\":\"disconnected\"}");

    _rebootPending = true;
    _rebootAt = millis() + 1000;  // Restart in AP mode after response delivers
}

// ── DNS Shield Handlers ──────────────────────────────────────────

void WebServer::_handleDnsGet(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
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
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    String profile = doc["profile"].is<const char*>() ? doc["profile"].as<String>() : dnsEngine.getActiveProfileKey();
    String primary = doc["customPrimary"].is<const char*>() ? doc["customPrimary"].as<String>() : (doc["primary"].is<const char*>() ? doc["primary"].as<String>() : "");
    String secondary = doc["customSecondary"].is<const char*>() ? doc["customSecondary"].as<String>() : (doc["secondary"].is<const char*>() ? doc["secondary"].as<String>() : "");
    bool haMode = doc["haMode"].is<bool>() ? doc["haMode"].as<bool>() : (doc["hybridDns"].is<bool>() ? doc["hybridDns"].as<bool>() : zteClient.isHaMode());

    if (profile.equalsIgnoreCase("custom")) {
        dnsEngine.setCustomUpstreams(primary, secondary.length() > 0 ? secondary : "0.0.0.0");
    } else {
        dnsEngine.setProfile(profile);
    }
    if (doc["blockMeta"].is<bool>() || doc["blockTiktok"].is<bool>() || doc["customDomains"].is<const char*>()) {
        DnsShieldRules cur;
        dnsEngine.getShieldRules(&cur);
        bool bm = doc["blockMeta"].is<bool>() ? doc["blockMeta"].as<bool>() : cur.blockMeta;
        bool bt = doc["blockTiktok"].is<bool>() ? doc["blockTiktok"].as<bool>() : cur.blockTiktok;
        String cd = doc["customDomains"].is<const char*>() ? doc["customDomains"].as<String>() : String(cur.customDomains);
        dnsEngine.setShieldRules(bm, bt, cd);
    }

    // Sync with ZTE Router DHCP
    bool routerOk = zteClient.syncRouterDnsProfile(profile, primary, secondary, haMode);
    Preferences userDnsPrefs;
    userDnsPrefs.begin("microrouter", false);
    userDnsPrefs.putBool("dns_user_ha", haMode);
    userDnsPrefs.putBool("dns_user_ha_set", true);
    userDnsPrefs.end();

    JsonDocument resp;
    resp["status"] = "ok";
    resp["routerSynced"] = routerOk;
    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleDnsQueries(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    request->send(200, "application/json", dnsEngine.getRecentQueriesJson());
}

void WebServer::_handleDnsQueriesClear(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    dnsEngine.clearQueryLog();
    request->send(200, "application/json", "{\"status\":\"cleared\"}");
}

// ── Device Management Handlers ───────────────────────────────────

void WebServer::_handleDevices(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    request->send(200, "application/json", deviceManager.getDevicesJson());
}

void WebServer::_handleDeviceBlock(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        bool ok = deviceManager.setBlocked(mac, true);
        if (ok) scheduler.revokeWaiver(mac);
        request->send(ok ? 200 : 404, "application/json",
                      "{\"status\":\"" + String(ok ? "blocked" : "not_found") +
                      "\",\"mac\":\"" + mac + "\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleDeviceAllow(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        bool ok = deviceManager.setBlocked(mac, false);
        if (ok) scheduler.revokeWaiver(mac);
        request->send(ok ? 200 : 404, "application/json",
                      "{\"status\":\"" + String(ok ? "allowed" : "not_found") +
                      "\",\"mac\":\"" + mac + "\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleDeviceWaiver(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        uint32_t secs = 0;
        bool hasDuration = false;

        if (doc["durationSecs"].is<uint32_t>()) {
            secs = doc["durationSecs"].as<uint32_t>();
            hasDuration = true;
        } else if (doc["minutes"].is<uint32_t>() || doc["minutes"].is<int>()) {
            secs = doc["minutes"].as<uint32_t>() * 60;
            hasDuration = true;
        }

        if (!hasDuration) {
            secs = 1800; // default 30 mins if omitted
        }

        if (secs == 0) {
            scheduler.revokeWaiver(mac);
            request->send(200, "application/json",
                          "{\"status\":\"waiver_revoked\",\"mac\":\"" + mac + "\"}");
            return;
        }

        scheduler.grantWaiver(mac, secs);
        request->send(200, "application/json",
                      "{\"status\":\"waiver_granted\",\"mac\":\"" + mac +
                      "\",\"durationSecs\":" + String(secs) + "}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleDeviceParental(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        String mac = doc["mac"].as<String>();
        bool enabled = doc["enabled"] | (doc["parentalControl"] | true);
        bool ok = deviceManager.setParentalControl(mac, enabled);
        request->send(ok ? 200 : 404, "application/json",
                      "{\"status\":\"" + String(ok ? "ok" : "not_found") +
                      "\",\"mac\":\"" + mac +
                      "\",\"parentalControl\":" + String(enabled ? "true" : "false") + "}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

// ── Curfew & Quota Handlers ──────────────────────────────────────

void WebServer::_handleCurfewGet(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    CurfewSchedule cs;
    scheduler.getCurfew(&cs);
    doc["enabled"]   = cs.enabled;
    doc["startHour"] = cs.startHour;
    doc["startMin"]  = cs.startMin;
    doc["endHour"]   = cs.endHour;
    doc["endMin"]    = cs.endMin;
    doc["activeNow"] = scheduler.isCurfewActive();
    doc["curfewEnabled"] = cs.enabled;
    doc["curfewActive"] = scheduler.isCurfewActive();
    doc["currentTime"] = scheduler.getTimeOnly();
    doc["timeStr"]   = scheduler.getFormattedTime();
    doc["timeOnly"]  = scheduler.getTimeOnly();
    doc["timeSynced"] = scheduler.isTimeSynced();

    QuotaLimits q;
    scheduler.getQuotas(&q);
    doc["hourlyEnabled"]   = q.hourlyEnabled;
    doc["hourlyLimitMb"]   = (uint32_t)(q.hourlyLimitBytes / (1024 * 1024));
    doc["hourlyQuotaMB"]    = (uint32_t)(q.hourlyLimitBytes / (1024 * 1024));
    doc["dailyEnabled"]    = q.dailyEnabled;
    doc["dailyLimitMb"]    = (uint32_t)(q.dailyLimitBytes / (1024 * 1024));
    doc["dailyQuotaMB"]     = (uint32_t)(q.dailyLimitBytes / (1024 * 1024));
    doc["timeEnabled"]     = q.timeEnabled;
    doc["dailyActiveMins"] = q.dailyActiveSecs / 60;

    uint64_t guestRxBytes = 0;
    uint64_t guestTxBytes = 0;
    deviceManager.getGuestUsage(&guestRxBytes, &guestTxBytes);
    doc["guestRxBytes"] = guestRxBytes;
    doc["guestTxBytes"] = guestTxBytes;
    doc["maxWaiversPerDay"] = scheduler.getMaxWaiversPerDay();
    doc["hasParentalPin"]   = scheduler.hasParentalPin();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleCurfewSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        bool en = doc["enabled"] | (doc["curfewEnabled"] | false);
        int sH = doc["startHour"] | 23;
        int sM = doc["startMin"] | 0;
        int eH = doc["endHour"] | 6;
        int eM = doc["endMin"] | 0;
        scheduler.setCurfew(en, sH, sM, eH, eM);

        if (doc["maxWaiversPerDay"].is<uint8_t>()) {
            scheduler.setMaxWaiversPerDay(doc["maxWaiversPerDay"].as<uint8_t>());
        }
        if (doc["parentalPin"].is<const char*>()) {
            scheduler.setParentalPin(doc["parentalPin"].as<String>());
        }

        bool routerSynced = true;
        Preferences prefs;
        prefs.begin("microrouter", false);
        if (en) {
            if (!prefs.getBool("curfew_dns_forced", false)) {
                // Keep the user's preference separate from the temporary strict mode.
                bool hasUserDnsPreference = prefs.getBool("dns_user_ha_set", false);
                prefs.putBool("curfew_prev_ha",
                              hasUserDnsPreference ? prefs.getBool("dns_user_ha", true) : true);
                prefs.putBool("curfew_dns_forced", true);
            }
            DnsStatsSnapshot stats;
            dnsEngine.getStats(&stats);
            // A DHCP secondary upstream bypasses curfew enforcement. Use strict
            // DNS while parental controls are enabled.
            routerSynced = zteClient.syncRouterDnsProfile(
                stats.profileKey, stats.upstreamPrimary, stats.upstreamSecondary, false);
        } else {
            DnsStatsSnapshot stats;
            dnsEngine.getStats(&stats);
            // Curfew's strict DNS is temporary; disabling curfew restores the
            // normal upstream fallback so clients do not remain on 0.0.0.0.
            bool previousHaMode = true;
            routerSynced = zteClient.syncRouterDnsProfile(
                stats.profileKey, stats.upstreamPrimary, stats.upstreamSecondary, previousHaMode);
            prefs.putBool("curfew_dns_forced", false);
        }
        prefs.end();

        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"routerSynced\":" +
                      String(routerSynced ? "true" : "false") + "}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleQuotaSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
        uint64_t hMb = doc["hourlyLimitMb"] | (doc["hourlyQuotaMB"] | 500);
        uint64_t dMb = doc["dailyLimitMb"] | (doc["dailyQuotaMB"] | 2048);
        uint32_t tm  = (doc["dailyActiveMins"] | 60) * 60;
        bool hourlyEnabled = doc["hourlyEnabled"] | (doc["hourlyQuotaMB"].is<uint64_t>() && hMb > 0);
        bool dailyEnabled = doc["dailyEnabled"] | (doc["dailyQuotaMB"].is<uint64_t>() && dMb > 0);
        scheduler.setQuotas(hourlyEnabled, hMb * 1024 * 1024ULL,
                    dailyEnabled, dMb * 1024 * 1024ULL,
                            doc["timeEnabled"] | false, tm);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
}

void WebServer::_handleCaptiveProbe(AsyncWebServerRequest* request) {
    String clientIp = request->client() ? request->client()->remoteIP().toString() : "";
    bool restricted = (clientIp.length() > 0) && deviceManager.isClientRestricted(clientIp);

    if (restricted) {
        request->redirect("http://" + _wifiMgr->getIP() + "/quota-notice");
        return;
    }

    String url = request->url();
    if (url.indexOf("hotspot-detect") >= 0) {
        request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else if (url.indexOf("connecttest") >= 0) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    } else if (url.indexOf("ncsi") >= 0) {
        request->send(200, "text/plain", "Microsoft NCSI");
    } else if (url.indexOf("canonical") >= 0 || url.indexOf("success") >= 0) {
        request->send(200, "text/plain", "success\n");
    } else {
        request->send(204);
    }
}

void WebServer::_handleQuotaNotice(AsyncWebServerRequest* request) {
    String clientIp = request->client() ? request->client()->remoteIP().toString() : "";
    String mac = "";
    String hostname = "";
    uint64_t dailyBytes = 0;
    ClientRestrictionReason reason = RESTRICTION_NONE;
    uint32_t waiverRemaining = 0;

    if (clientIp.length() > 0) {
        deviceManager.getClientQuotaStatus(clientIp, mac, hostname, dailyBytes, reason, waiverRemaining);
    }

    QuotaLimits quotas;
    scheduler.getQuotas(&quotas);
    uint8_t waiversUsedToday = (mac.length() > 0) ? scheduler.getWaiverCountToday(mac) : 0;
    uint8_t maxWaiversPerDay = scheduler.getMaxWaiversPerDay();
    bool pinConfigured = scheduler.hasParentalPin();

    String html = buildQuotaNoticeHtml(
        clientIp,
        mac,
        hostname,
        reason,
        dailyBytes,
        quotas.dailyEnabled ? quotas.dailyLimitBytes : 0,
        waiverRemaining,
        waiversUsedToday,
        maxWaiversPerDay,
        pinConfigured
    );

    AsyncWebServerResponse* resp = request->beginResponse(200, "text/html; charset=utf-8", html);
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    request->send(resp);
}

void WebServer::_handleRequestWaiver(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;

    String clientIp = request->client() ? request->client()->remoteIP().toString() : "";
    String mac = "";
    String hostname = "";
    String pin = "";
    uint64_t dailyBytes = 0;
    uint32_t secs = 1800; // default 30 mins

    if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            const char* m = doc["mac"] | "";
            if (strlen(m) > 0) mac = String(m);

            const char* p = doc["pin"] | "";
            if (strlen(p) > 0) pin = String(p);
            if (doc["durationSecs"].is<uint32_t>()) {
                secs = doc["durationSecs"].as<uint32_t>();
            } else if (doc["minutes"].is<uint32_t>() || doc["minutes"].is<int>()) {
                secs = doc["minutes"].as<uint32_t>() * 60;
            }
        }
    }

    if (mac.length() == 0 && clientIp.length() > 0) {
        bool found = deviceManager.getClientDetailsByIp(clientIp, mac, hostname, dailyBytes);
        if (!found || mac.length() == 0) {
            deviceManager.registerClientActivity(clientIp.c_str());
            deviceManager.getClientDetailsByIp(clientIp, mac, hostname, dailyBytes);
        }
    }

    if (mac.length() == 0) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unable to determine device MAC\"}");
        return;
    }

    bool isAdminOverride = false;
    if (pin.length() > 0) {
        if (scheduler.hasParentalPin()) {
            if (scheduler.verifyParentalPin(pin)) {
                isAdminOverride = true;
            } else {
                request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Invalid Parent PIN\"}");
                return;
            }
        } else {
            isAdminOverride = true;
        }
    } else {
        uint8_t used = scheduler.getWaiverCountToday(mac);
        uint8_t maxW = scheduler.getMaxWaiversPerDay();
        if (used >= maxW) {
            request->send(403, "application/json",
                          "{\"status\":\"error\",\"message\":\"Daily extension limit reached. Parent PIN required.\"}");
            return;
        }
    }

    bool ok = scheduler.grantWaiver(mac, secs, isAdminOverride);
    if (!ok) {
        request->send(403, "application/json",
                      "{\"status\":\"error\",\"message\":\"Daily extension limit reached.\"}");
        return;
    }

    deviceManager.invalidateRestrictionCache();
    Serial.printf("[Waiver] Emergency waiver granted: %s (%s) for %u secs (admin override: %d)\n",
                  mac.c_str(), clientIp.c_str(), secs, isAdminOverride);

    JsonDocument resp;
    resp["status"] = "ok";
    resp["mac"] = mac;
    resp["ip"] = clientIp;
    resp["hostname"] = hostname;
    resp["grantedSecs"] = secs;
    resp["waiversUsedToday"] = scheduler.getWaiverCountToday(mac);
    resp["maxWaiversPerDay"] = scheduler.getMaxWaiversPerDay();

    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
}

// ── Router Gateway Parity Handlers ───────────────────────────────

void WebServer::_handleRouterReboot(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    bool systemOnly = request->hasParam("system");
    bool ok = systemOnly ? true : zteClient.reboot();
    if (systemOnly) {
        _rebootPending = true;
        _rebootAt = millis() + 1000;
    }
    request->send(ok ? 200 : 502, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + "}");
}

void WebServer::_handleRouterWifiToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
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
    request->send(ok ? 200 : 502, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + ",\"state\":\"" + (on ? "1" : "0") + "\"}");
}

void WebServer::_handleRouterSsidToggle(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
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
    request->send(ok ? 200 : 502, "application/json", "{\"ok\":" + String(ok ? "true" : "false") + "}");
}

void WebServer::_handleRouterDnsGet(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    DnsStatsSnapshot stats;
    dnsEngine.getStats(&stats);

    JsonDocument doc;
    doc["ok"] = true;
    doc["profile"] = stats.profileKey;
    doc["primary"] = stats.upstreamPrimary;
    doc["secondary"] = stats.upstreamSecondary;
    doc["dhcpPrimary"] = _wifiMgr->getIP();
    doc["dhcpSecondary"] = zteClient.getRouterDnsSecondary();
    doc["hybridDns"] = zteClient.isHaMode();
    doc["haMode"] = zteClient.isHaMode();
    doc["routerSynced"] = zteClient.isDnsSynced();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleRouterDnsSet(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
    String profile = "ultra_fast";
    String primary = "";
    String secondary = "";
    bool haMode = zteClient.isHaMode();

    if (request->hasParam("profile")) {
        profile = request->getParam("profile")->value();
    }
    if (data && len > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (doc["profile"].is<const char*>()) profile = doc["profile"].as<String>();
            if (doc["primary"].is<const char*>()) primary = doc["primary"].as<String>();
            if (doc["secondary"].is<const char*>()) secondary = doc["secondary"].as<String>();
            if (doc["haMode"].is<bool>()) haMode = doc["haMode"].as<bool>();
            else if (doc["hybridDns"].is<bool>()) haMode = doc["hybridDns"].as<bool>();
        }
    }

    bool ok = zteClient.syncRouterDnsProfile(profile, primary, secondary, haMode);
    Preferences userDnsPrefs;
    userDnsPrefs.begin("microrouter", false);
    userDnsPrefs.putBool("dns_user_ha", haMode);
    userDnsPrefs.putBool("dns_user_ha_set", true);
    userDnsPrefs.end();

    DnsStatsSnapshot stats;
    dnsEngine.getStats(&stats);

    JsonDocument resp;
    resp["ok"] = ok;
    resp["profile"] = stats.profileKey;
    resp["primary"] = stats.upstreamPrimary;
    resp["secondary"] = stats.upstreamSecondary;
    resp["dhcpPrimary"] = _wifiMgr->getIP();
    resp["dhcpSecondary"] = zteClient.getRouterDnsSecondary();
    resp["hybridDns"] = zteClient.isHaMode();
    resp["haMode"] = zteClient.isHaMode();
    resp["routerSynced"] = ok;

    String out;
    serializeJson(resp, out);
    request->send(ok ? 200 : 502, "application/json", out);
}

void WebServer::_handleRouterDnsVerify(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    String p, s;
    int src = -1;
    bool ok = zteClient.fetchRouterDhcpDnsSettings(p, s, src);

    JsonDocument doc;
    doc["ok"] = ok;
    doc["live_dhcp_dns1"] = p;
    doc["live_dhcp_dns2"] = s;
    doc["live_dns_source"] = src;
    doc["esp32_ip"] = _wifiMgr->getIP();
    doc["cached_synced"] = zteClient.isDnsSynced();
    doc["match"] = (p == _wifiMgr->getIP());

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebServer::_handleGuestAnalytics(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    String json = deviceManager.getGuestAnalyticsJson();
    request->send(200, "application/json", json);
}

void WebServer::_handleGuestAnalyticsDelete(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!_checkRateLimit(request)) return;
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
    if (!_checkRateLimit(request)) return;
    deviceManager.clearGuestUsage();
    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::_handleLastLog(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
    request->send(200, "text/plain", zteClient.getLastLog());
}

void WebServer::_handleSession(AsyncWebServerRequest* request) {
    if (!_checkRateLimit(request)) return;
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

    // For API routes — return JSON 404 (safely serialized)
    if (request->url().startsWith("/api/")) {
        JsonDocument doc;
        doc["error"] = "Not found";
        doc["path"]  = request->url();
        String json;
        serializeJson(doc, json);
        request->send(404, "application/json", json);
        return;
    }

    // If client is restricted, redirect any unmapped HTTP request to the quota notice
    String clientIp = request->client() ? request->client()->remoteIP().toString() : "";
    if (clientIp.length() > 0 && deviceManager.isClientRestricted(clientIp)) {
        if (!request->url().equals("/quota-notice")) {
            request->redirect("http://" + _wifiMgr->getIP() + "/quota-notice");
            return;
        }
    }

    // For all other routes — serve index.html (SPA fallback)
    request->send(LittleFS, "/index.html", "text/html");
}

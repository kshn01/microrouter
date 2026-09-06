#include "zte_client.h"
#include "device_manager.h"
#include "config.h"

ZteRouterClient zteClient;

ZteRouterClient::ZteRouterClient() {
    _routerIp = ROUTER_GATEWAY_IP;
    _routerUser = "admin";
    _routerPass = "admin";
}

ZteRouterClient::~ZteRouterClient() {
    if (_httpMutex) {
        vSemaphoreDelete(_httpMutex);
        _httpMutex = nullptr;
    }
}

bool ZteRouterClient::begin() {
    if (!_httpMutex) {
        _httpMutex = xSemaphoreCreateRecursiveMutex();
    }
    Serial.println("[ZTE] ZteRouterClient initialized.");
    return true;
}

String ZteRouterClient::_xmlExtract(const String& xml, const String& tag) {
    String openTag  = "<" + tag + ">";
    String closeTag = "</" + tag + ">";
    int start = xml.indexOf(openTag);
    if (start < 0) return "";
    start += openTag.length();
    int end = xml.indexOf(closeTag, start);
    if (end < 0) return "";
    return xml.substring(start, end);
}

String ZteRouterClient::_getRawInitialSID() {
    WiFiClient client;
    if (!client.connect(_routerIp.c_str(), 80)) {
        Serial.println("[ZTE] Raw TCP connect failed to " + _routerIp);
        return "";
    }
    client.print(String("GET / HTTP/1.1\r\nHost: ") + _routerIp +
                 "\r\nUser-Agent: Mozilla/5.0 (ESP32)\r\nConnection: close\r\n\r\n");

    String sid = "";
    unsigned long t = millis();
    while (client.connected() && (millis() - t < 8000)) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;
            if (line.startsWith("Set-Cookie:") || line.startsWith("set-cookie:")) {
                int sp = line.indexOf("SID=");
                if (sp >= 0) {
                    sp += 4;
                    int se = line.indexOf(";", sp);
                    if (se < 0) se = line.length();
                    sid = line.substring(sp, se);
                    sid.trim();
                }
            }
        } else {
            delay(1);
        }
    }

    uint8_t drainBuf[512];
    unsigned long dt = millis();
    while ((client.connected() || client.available()) && (millis() - dt < 3000)) {
        while (client.available()) {
            client.readBytes(drainBuf, sizeof(drainBuf));
        }
        delay(1);
    }
    client.stop();
    return sid;
}

String ZteRouterClient::_extractTokenFromStream(WiFiClient* stream, unsigned long maxWaitMs) {
    if (!stream) return "";
    unsigned long start = millis();
    enum State { SEARCH_NAME, FIND_QUOTE, EXTRACT_VAL };
    State state = SEARCH_NAME;
    
    char tokenBuf[128];
    int tokenLen = 0;
    char window[20];
    int wLen = 0;
    auto pushChar = [&](char c) {
        if (wLen == 19) {
            memmove(window, window + 1, 18);
            window[18] = c;
            window[19] = '\0';
        } else {
            window[wLen++] = c;
            window[wLen] = '\0';
        }
    };
    
    while (millis() - start < maxWaitMs) {
        int avail = stream->available();
        if (avail > 0) {
            char buf[128];
            int toRead = min(avail, (int)sizeof(buf));
            toRead = stream->readBytes(buf, toRead);
            for (int i = 0; i < toRead; i++) {
                char c = buf[i];
                if (state == SEARCH_NAME) {
                    pushChar(c);
                    if (strstr(window, "_sessionTmpToken")) {
                        state = FIND_QUOTE;
                    }
                } else if (state == FIND_QUOTE) {
                    if (c == '"') {
                        state = EXTRACT_VAL;
                    }
                } else if (state == EXTRACT_VAL) {
                    if (c == '"') {
                        tokenBuf[tokenLen] = '\0';
                        return CryptoHelpers::decodeHexEscapes(String(tokenBuf));
                    } else {
                        if (tokenLen < (int)sizeof(tokenBuf) - 1) {
                            tokenBuf[tokenLen++] = c;
                        }
                    }
                }
            }
        } else {
            delay(1);
        }
    }
    return "";
}

String ZteRouterClient::routerGET(const String& path) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_MODE_NULL) return "";
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    HTTPClient http;
    http.begin(String("http://") + _routerIp + path);
    http.addHeader("Connection",       "close");
    http.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Referer",          String("http://") + _routerIp + "/");
    String ck = "_TESTCOOKIESUPPORT=1";
    if (_sidCookie.length() > 0) ck = "SID=" + _sidCookie + "; " + ck;
    http.addHeader("Cookie", ck);
    http.setTimeout(8000);
    int code = http.GET();
    String body = "";
    if (code > 0) body = http.getString();
    http.end();
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return body;
}

bool ZteRouterClient::login() {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    Serial.println("[ZTE] Starting login sequence to " + _routerIp);
    _sidCookie = "";
    _sessToken = "";

    _sidCookie = _getRawInitialSID();
    if (_sidCookie.length() == 0) {
        Serial.println("[ZTE] Failed to get initial SID");
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    String tokenBody = routerGET("/?_type=loginData&_tag=login_token");
    String xmlToken  = _xmlExtract(tokenBody, "ajax_response_xml_root");
    xmlToken.trim();
    if (xmlToken.length() == 0) {
        Serial.println("[ZTE] No login token returned!");
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    String hashedPass = CryptoHelpers::sha256Hex(_routerPass + xmlToken);
    String postData   = "action=login&Username=" + _routerUser +
                        "&Password=" + hashedPass + "&_sessionTOKEN=";
    {
        const char* ph[] = {"Set-Cookie"};
        HTTPClient h3;
        h3.begin(String("http://") + _routerIp + "/?_type=loginData&_tag=login_entry");
        h3.addHeader("Connection",       "close");
        h3.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
        h3.addHeader("X-Requested-With", "XMLHttpRequest");
        h3.addHeader("Content-Type",     "application/x-www-form-urlencoded");
        h3.addHeader("Referer",          String("http://") + _routerIp + "/");
        h3.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
        h3.collectHeaders(ph, 1);
        h3.setTimeout(8000);
        int c3 = h3.POST(postData);
        String resp = (c3 > 0) ? h3.getString() : "";

        String ck = h3.header("Set-Cookie");
        int sp = ck.indexOf("SID=");
        if (sp >= 0) {
            sp += 4;
            int se = ck.indexOf(";", sp);
            if (se < 0) se = ck.length();
            String newSID = ck.substring(sp, se);
            newSID.trim();
            if (newSID.length() > 0) _sidCookie = newSID;
        }
        h3.end();

        int ti = resp.indexOf("\"sess_token\":\"");
        if (ti < 0) {
            Serial.println("[ZTE] Login failed, no sess_token in response: " + resp);
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }
        ti += 14;
        _sessToken = resp.substring(ti, resp.indexOf("\"", ti));
    }

    _loggedIn = true;
    _lastLoginTime = millis();
    _lastLog = "ZTE Gateway login successful (" + _routerIp + ")";
    Serial.println("[ZTE] Login successful! SessToken: " + _sessToken.substring(0, 8) + "...");

    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return true;
}

bool ZteRouterClient::relogin() {
    _loggedIn = false;
    return login();
}

void ZteRouterClient::fetchRealTimeStats() {
    if (!_loggedIn) {
        if (!login()) return;
    }
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);

    routerGET("/?_type=menuView&_tag=localNetStatus&Menu3Location=0");
    delay(50);

    HTTPClient h;
    h.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=wlan_client_stat_lua.lua");
    h.addHeader("Connection", "close");
    h.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    h.setTimeout(8000);
    int code = h.GET();

    if (code == 200) {
        WiFiClient* stream = h.getStreamPtr();
        unsigned long start = millis();
        enum State { SEARCH_INST, IN_INST, SEARCH_VAL, READ_VAL };
        State state = SEARCH_INST;
        String currentField = "";
        String mac = "", hostname = "", ip = "", aliasName = "";
        int rssiVal = 0;
        uint64_t txBytes = 0, rxBytes = 0;

        char valBuf[128];
        int valLen = 0;
        char window[16];
        int wLen = 0;
        auto pushChar = [&](char c) {
            if (wLen == 15) {
                memmove(window, window + 1, 14);
                window[14] = c;
                window[15] = '\0';
            } else {
                window[wLen++] = c;
                window[wLen] = '\0';
            }
        };

        while (millis() - start < 8000) {
            int avail = stream->available();
            if (avail > 0) {
                char buf[128];
                int toRead = min(avail, (int)sizeof(buf));
                toRead = stream->readBytes(buf, toRead);
                for (int i = 0; i < toRead; i++) {
                    char c = buf[i];
                    pushChar(c);

                    if (strstr(window, "SessionTimeout")) {
                        _loggedIn = false;
                        break;
                    }

                    if (state == SEARCH_INST) {
                        if (strstr(window, "<Instance>")) {
                            mac = ""; hostname = ""; ip = ""; aliasName = ""; rssiVal = 0; txBytes = 0; rxBytes = 0;
                            state = IN_INST; wLen = 0; window[0] = '\0';
                        }
                    } else if (state == IN_INST) {
                        if (strstr(window, "</Instance>")) {
                            if (mac.length() > 0) {
                                String band = "2.4G";
                                if (aliasName.startsWith("DEV.WIFI.AP")) {
                                    int apNum = aliasName.substring(11).toInt();
                                    if (apNum == 1) band = "Guest";
                                    else if (apNum >= 2 && apNum <= 4) band = "2.4G";
                                    else if (apNum >= 5 && apNum <= 8) band = "5G";
                                }
                                deviceManager.updateDevice(mac, ip, hostname, band, (int8_t)rssiVal, true, rxBytes, txBytes);
                            }
                            state = SEARCH_INST; wLen = 0; window[0] = '\0';
                        } else if (strstr(window, "<ParaName>")) {
                            state = SEARCH_VAL; currentField = ""; valLen = 0; valBuf[0] = '\0'; wLen = 0; window[0] = '\0';
                        }
                    } else if (state == SEARCH_VAL) {
                        if (valLen < (int)sizeof(valBuf) - 1) { valBuf[valLen++] = c; valBuf[valLen] = '\0'; }
                        if (strstr(window, "</ParaName>")) {
                            if (valLen >= 11) valBuf[valLen - 11] = '\0';
                            currentField = String(valBuf);
                            wLen = 0; window[0] = '\0';
                        } else if (strstr(window, "<ParaValue>")) {
                            state = READ_VAL; valLen = 0; valBuf[0] = '\0'; wLen = 0; window[0] = '\0';
                        }
                    } else if (state == READ_VAL) {
                        if (valLen < (int)sizeof(valBuf) - 1) { valBuf[valLen++] = c; valBuf[valLen] = '\0'; }
                        if (strstr(window, "</ParaValue>")) {
                            if (valLen >= 12) valBuf[valLen - 12] = '\0';
                            String currentVal = String(valBuf);
                            if (currentField == "MACAddress") mac = currentVal;
                            else if (currentField == "HostName") hostname = currentVal;
                            else if (currentField == "IPAddress") ip = currentVal;
                            else if (currentField == "AliasName") aliasName = currentVal;
                            else if (currentField == "RSSI") rssiVal = currentVal.toInt();
                            else if (currentField == "TXBytes") txBytes = strtoull(currentVal.c_str(), nullptr, 10);
                            else if (currentField == "RXBytes") rxBytes = strtoull(currentVal.c_str(), nullptr, 10);
                            state = IN_INST; wLen = 0; window[0] = '\0';
                        }
                    }
                }
                if (!_loggedIn) break;
            } else {
                delay(1);
            }
            if (!h.connected() && stream->available() == 0) break;
        }
    }
    h.end();
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
}

void ZteRouterClient::fetchRouterHealth() {
    if (!_loggedIn) return;
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);

    routerGET("/?_type=menuView&_tag=statusMgr&Menu3Location=0");
    delay(50);
    HTTPClient h;
    h.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=devmgr_statusmgr_lua.lua");
    h.addHeader("Connection", "close");
    h.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    h.setTimeout(8000);
    int code = h.GET();

    if (code == 200) {
        String resp = h.getString();
        int cpuPos = resp.indexOf("CpuUsage");
        if (cpuPos > 0) {
            int vStart = resp.indexOf("<ParaValue>", cpuPos);
            int vEnd = resp.indexOf("</ParaValue>", vStart);
            if (vStart > 0 && vEnd > vStart) {
                _routerCpu = resp.substring(vStart + 11, vEnd).toInt();
            }
        }
        int memPos = resp.indexOf("MemUsage");
        if (memPos > 0) {
            int vStart = resp.indexOf("<ParaValue>", memPos);
            int vEnd = resp.indexOf("</ParaValue>", vStart);
            if (vStart > 0 && vEnd > vStart) {
                _routerMem = resp.substring(vStart + 11, vEnd).toInt();
            }
        }
    }
    h.end();
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
}

bool ZteRouterClient::reboot() {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) login();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    HTTPClient hCtx;
    hCtx.begin("http://" + _routerIp + "/?_type=menuView&_tag=rebootAndReset&Menu3Location=0");
    hCtx.addHeader("Referer", "http://" + _routerIp + "/");
    hCtx.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    hCtx.setTimeout(5000);
    int code = hCtx.GET();
    String tmpToken = "";
    if (code == 200) {
        tmpToken = _extractTokenFromStream(hCtx.getStreamPtr(), 6000);
    }
    hCtx.end();

    if (tmpToken.isEmpty()) tmpToken = _sessToken;

    String body = "IF_ACTION=Restart&_sessionTOKEN=" + tmpToken;
    String bodyHash = CryptoHelpers::sha256Hex(body);
    String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

    HTTPClient http;
    http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=devmgr_restartmgr_lua.lua");
    http.addHeader("Connection", "close");
    http.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Referer", String("http://") + _routerIp + "/");
    http.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    http.addHeader("Check", checkHdr);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int postCode = http.POST(body);
    String resp = (postCode > 0) ? http.getString() : "";
    http.end();

    _lastLog = "ZTE Reboot Command Sent (HTTP " + String(postCode) + ")";
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return (postCode > 0);
}

bool ZteRouterClient::toggleRadio(bool enable) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) login();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    HTTPClient hCtx;
    hCtx.begin("http://" + _routerIp + "/?_type=menuView&_tag=wlanBasic&Menu3Location=0");
    hCtx.addHeader("Connection",       "close");
    hCtx.addHeader("Referer",          "http://" + _routerIp + "/");
    hCtx.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    hCtx.setTimeout(6000);
    int code = hCtx.GET();
    String token = "";
    if (code == 200) {
        token = _extractTokenFromStream(hCtx.getStreamPtr(), 6000);
    }
    hCtx.end();

    if (token.isEmpty()) token = _sessToken;

    String body = "IF_ACTION=Apply&Enable=" + String(enable ? "1" : "0") + "&_InstID=DEV.WIFI.RAD1&_WEPCONIG=N&_PSKCONIG=N";
    body += "&_sessionTOKEN=" + token;
    String bodyHash = CryptoHelpers::sha256Hex(body);
    String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

    HTTPClient http;
    http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=wlan_wlanbasic_lua.lua");
    http.addHeader("Connection",       "close");
    http.addHeader("Content-Type",     "application/x-www-form-urlencoded");
    http.addHeader("Referer",          String("http://") + _routerIp + "/");
    http.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    http.addHeader("Check",            checkHdr);
    http.setTimeout(8000);
    int postCode = http.POST(body);
    String resp = (postCode > 0) ? http.getString() : "";
    http.end();

    bool ok = (resp.indexOf("<IF_ERRORTYPE>SUCC</IF_ERRORTYPE>") >= 0);
    _lastLog = "WiFi Radio Toggle (" + String(enable ? "ON" : "OFF") + ") -> " + (ok ? "OK" : "FAILED");
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return ok;
}

bool ZteRouterClient::toggleSSID(int ssidIdx, bool enable) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) login();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    String instID = "DEV.WIFI.AP" + String(ssidIdx);
    int tokenPageIdx = (ssidIdx <= 1) ? 0 : (ssidIdx - 1);
    String pageUrl = "/?_type=menuView&_tag=wlanBasic&wlan_ssidIdx=" + String(tokenPageIdx) + "&Menu3Location=0";

    String token = "";
    {
        HTTPClient hCtx;
        hCtx.begin("http://" + _routerIp + pageUrl);
        hCtx.addHeader("Connection", "close");
        hCtx.addHeader("Referer",    "http://" + _routerIp + "/");
        hCtx.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
        hCtx.setTimeout(8000);
        int code = hCtx.GET();
        if (code == 200) {
            token = _extractTokenFromStream(hCtx.getStreamPtr(), 6000);
        }
        hCtx.end();
    }
    if (token.isEmpty()) token = _sessToken;

    String body = "IF_ACTION=Apply&Enable=" + String(enable ? "1" : "0");
    body += "&_InstID=" + instID;
    body += "&_WEPCONIG=N&_PSKCONIG=N";
    body += "&_sessionTOKEN=" + token;

    String bodyHash = CryptoHelpers::sha256Hex(body);
    String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

    HTTPClient http;
    http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=wlan_wlansssidconf_lua.lua");
    http.addHeader("Connection",       "close");
    http.addHeader("Content-Type",     "application/x-www-form-urlencoded; charset=UTF-8");
    http.addHeader("Referer",          String("http://") + _routerIp + "/");
    http.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    http.addHeader("Check",            checkHdr);
    http.setTimeout(8000);

    int code = http.POST(body);
    String resp = (code > 0) ? http.getString() : "";
    http.end();

    bool ok = (resp.indexOf("IF_ERRORPARAM>SUCC<") >= 0 && resp.indexOf("<IF_ERRORID>0</IF_ERRORID>") >= 0);
    _lastLog = "SSID " + String(ssidIdx) + " Toggle (" + String(enable ? "ON" : "OFF") + ") -> " + (ok ? "OK" : "ERR");
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return ok;
}

bool ZteRouterClient::syncDns(const String& primaryDns, const String& secondaryDns) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) login();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    String body = "IF_ACTION=Apply&DNSServers=" + primaryDns + "," + secondaryDns;
    body += "&_sessionTOKEN=" + _sessToken;
    String bodyHash = CryptoHelpers::sha256Hex(body);
    String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

    HTTPClient http;
    http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=dhcp_dns_lua.lua");
    http.addHeader("Connection", "close");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    http.addHeader("Check", checkHdr);
    http.setTimeout(8000);
    int code = http.POST(body);
    http.end();

    _lastLog = "Router DHCP DNS Pushed: " + primaryDns + ", " + secondaryDns;
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return (code > 0);
}

void ZteRouterClient::getStats(int& cpu, int& mem, String& uptime) const {
    cpu = _routerCpu;
    mem = _routerMem;
    uptime = _routerUptime;
}

String ZteRouterClient::getLastLog() const {
    return _lastLog;
}

void ZteRouterClient::loop() {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        // Periodic stats poll every 15s
        if (now - _lastPollTime > 15000) {
            _lastPollTime = now;
            fetchRealTimeStats();
        }
        // Periodic health check every 30s
        if (now - _lastHealthTime > 30000) {
            _lastHealthTime = now;
            fetchRouterHealth();
        }
    }
}

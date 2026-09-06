#include "zte_client.h"
#include "device_manager.h"
#include "config.h"
#include "dns_engine.h"
#include <Preferences.h>

ZteRouterClient zteClient;

ZteRouterClient::ZteRouterClient() {
    _routerIp = ROUTER_GATEWAY_IP;
    _routerUser = ROUTER_GATEWAY_USER;
    _routerPass = ROUTER_GATEWAY_PASS;
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

// ─── Direct Router Native DNS Profile Management & Local Domain Registration ───

static String extractXmlParaValue(const String& xml, const String& paraName) {
    String openTag = "<ParaName>" + paraName + "</ParaName>";
    int p = xml.indexOf(openTag);
    if (p < 0) return "";
    int vStart = xml.indexOf("<ParaValue>", p + openTag.length());
    if (vStart < 0) return "";
    vStart += 11;
    int vEnd = xml.indexOf("</ParaValue>", vStart);
    if (vEnd < 0) return "";
    String val = xml.substring(vStart, vEnd);
    val.trim();
    return val;
}

static void splitIpIntoOctets(const String& ipStr, String octets[4]) {
    int start = 0;
    for (int i = 0; i < 4; i++) {
        int dot = ipStr.indexOf('.', start);
        if (dot == -1 || i == 3) {
            octets[i] = ipStr.substring(start);
            octets[i].trim();
            break;
        } else {
            octets[i] = ipStr.substring(start, dot);
            octets[i].trim();
            start = dot + 1;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (octets[i].isEmpty()) octets[i] = "0";
    }
}

String ZteRouterClient::getDnsContextToken() {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) relogin();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return "";
    }

    HTTPClient hCtx;
    hCtx.begin(String("http://") + _routerIp + "/?_type=menuView&_tag=dns&Menu3Location=0");
    hCtx.addHeader("Connection", "close");
    hCtx.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");
    hCtx.addHeader("X-Requested-With", "XMLHttpRequest");
    hCtx.addHeader("Referer", String("http://") + _routerIp + "/");
    hCtx.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    hCtx.setTimeout(8000);
    int code = hCtx.GET();
    String tmpToken = "";
    if (code == 200) {
        WiFiClient* stream = hCtx.getStreamPtr();
        tmpToken = _extractTokenFromStream(stream, 8000);
    }
    hCtx.end();

    if (tmpToken.isEmpty()) {
        tmpToken = _sessToken;
    }
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return tmpToken;
}

String ZteRouterClient::getLanIpv4ContextToken() {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) relogin();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return "";
    }

    HTTPClient hCtx;
    hCtx.begin(String("http://") + _routerIp + "/?_type=menuView&_tag=lanMgrIpv4&Menu3Location=0");
    hCtx.addHeader("Connection", "close");
    hCtx.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");
    hCtx.addHeader("X-Requested-With", "XMLHttpRequest");
    hCtx.addHeader("Referer", String("http://") + _routerIp + "/");
    hCtx.addHeader("Cookie", "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
    hCtx.setTimeout(8000);
    int code = hCtx.GET();
    String tmpToken = "";
    if (code == 200) {
        WiFiClient* stream = hCtx.getStreamPtr();
        tmpToken = _extractTokenFromStream(stream, 8000);
    }
    hCtx.end();

    if (tmpToken.isEmpty()) {
        tmpToken = _sessToken;
    }
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return tmpToken;
}

bool ZteRouterClient::fetchRouterDnsSettings(String& primaryDns, String& secondaryDns) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) relogin();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    getDnsContextToken();

    String xml = routerGET("/?_type=menuData&_tag=dns_localdns_lua.lua");
    if (xml.indexOf("SessionTimeout") >= 0) {
        _loggedIn = false;
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    primaryDns = extractXmlParaValue(xml, "SerIPAddress1");
    secondaryDns = extractXmlParaValue(xml, "SerIPAddress2");
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return (primaryDns.length() > 0 || secondaryDns.length() > 0);
}

bool ZteRouterClient::fetchRouterDhcpDnsSettings(String& primaryDns, String& secondaryDns, int& dnsSource) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    if (!_loggedIn) relogin();
    if (!_loggedIn) {
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    getLanIpv4ContextToken();

    String xml = routerGET("/?_type=menuData&_tag=Localnet_LanMgrIpv4_DHCPBasicCfg_lua.lua");
    if (xml.indexOf("SessionTimeout") >= 0) {
        _loggedIn = false;
        if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
        return false;
    }

    primaryDns = extractXmlParaValue(xml, "DNSServer1");
    secondaryDns = extractXmlParaValue(xml, "DNSServer2");
    String src = extractXmlParaValue(xml, "DnsServerSource");
    dnsSource = (src.length() > 0) ? src.toInt() : 0;
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return (primaryDns.length() > 0 || secondaryDns.length() > 0);
}

bool ZteRouterClient::applyRouterDns(const String& primaryDns, const String& secondaryDns) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!_loggedIn) relogin();
        if (!_loggedIn) {
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String token = getDnsContextToken();
        if (token.isEmpty()) {
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String body = "IF_ACTION=Apply&_InstID=IGD&SerIPAddress1=" + primaryDns +
                      "&SerIPAddress2=" + secondaryDns +
                      "&_sessionTOKEN=" + token;
        String bodyHash = CryptoHelpers::sha256Hex(body);
        String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

        HTTPClient http;
        http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=dns_localdns_lua.lua");
        http.addHeader("Connection",       "close");
        http.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
        http.addHeader("X-Requested-With", "XMLHttpRequest");
        http.addHeader("Content-Type",     "application/x-www-form-urlencoded; charset=UTF-8");
        http.addHeader("Referer",          String("http://") + _routerIp + "/");
        http.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
        http.addHeader("Check",            checkHdr);
        http.setTimeout(10000);

        int code = http.POST(body);
        String resp = (code > 0) ? http.getString() : "";
        http.end();

        if (resp.indexOf("SessionTimeout") >= 0) {
            _loggedIn = false;
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        bool ok = (resp.indexOf("SUCC") >= 0 || resp.indexOf("<IF_ERRORTYPE>SUCC</IF_ERRORTYPE>") >= 0);
        Serial.printf("[ROUTER-DNS] Apply Router Forwarder DNS (%s / %s): code=%d ok=%d\n",
                      primaryDns.c_str(), secondaryDns.c_str(), code, ok);
        if (ok) {
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return true;
        }
    }
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return false;
}

bool ZteRouterClient::applyRouterDhcpDns(const String& primaryDns, const String& secondaryDns) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!_loggedIn) relogin();
        if (!_loggedIn) {
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String token = getLanIpv4ContextToken();
        if (token.isEmpty()) {
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String currentXml = routerGET("/?_type=menuData&_tag=Localnet_LanMgrIpv4_DHCPBasicCfg_lua.lua");
        if (currentXml.indexOf("SessionTimeout") >= 0) {
            _loggedIn = false;
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String instId = extractXmlParaValue(currentXml, "_InstID");
        if (instId.isEmpty()) instId = "IGD";

        String ipAddr = extractXmlParaValue(currentXml, "IPAddr");
        if (ipAddr.isEmpty()) ipAddr = _routerIp;

        String subMask = extractXmlParaValue(currentXml, "SubMask");
        if (subMask.isEmpty()) subMask = "255.255.255.0";

        String subnetMask = extractXmlParaValue(currentXml, "SubnetMask");
        if (subnetMask.isEmpty()) subnetMask = subMask;

        String minAddress = extractXmlParaValue(currentXml, "MinAddress");
        if (minAddress.isEmpty()) minAddress = "192.168.1.2";

        String maxAddress = extractXmlParaValue(currentXml, "MaxAddress");
        if (maxAddress.isEmpty()) maxAddress = "192.168.1.254";

        String serverEnable = extractXmlParaValue(currentXml, "ServerEnable");
        if (serverEnable.isEmpty()) serverEnable = "1";

        String leaseTime = extractXmlParaValue(currentXml, "LeaseTime");
        if (leaseTime.isEmpty()) leaseTime = "86400";

        String primDns = primaryDns;
        primDns.trim();
        if (primDns.isEmpty()) primDns = "1.1.1.1";

        String secDns = secondaryDns;
        secDns.trim();
        if (secDns.isEmpty()) secDns = "0.0.0.0";

        String currDns1 = extractXmlParaValue(currentXml, "DNSServer1");
        String currDns2 = extractXmlParaValue(currentXml, "DNSServer2");
        String currSrc  = extractXmlParaValue(currentXml, "DnsServerSource");
        if (currSrc == "0" && currDns1 == primDns && currDns2 == secDns) {
            Serial.printf("[ROUTER-DHCP] DHCP DNS already matches target (%s / %s, DnsServerSource=0). Skipping redundant POST.\n",
                          primDns.c_str(), secDns.c_str());
            _dnsSynced = true;
            _routerDnsPrimary = primDns;
            _routerDnsSecondary = secDns;
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return true;
        }

        String subIp[4], subSub[4], subMin[4], subMax[4], subDns1[4], subDns2[4];
        splitIpIntoOctets(ipAddr, subIp);
        splitIpIntoOctets(subMask, subSub);
        splitIpIntoOctets(minAddress, subMin);
        splitIpIntoOctets(maxAddress, subMax);
        splitIpIntoOctets(primDns, subDns1);
        splitIpIntoOctets(secDns, subDns2);

        String body = "IF_ACTION=Apply";
        body += "&IF_URL_HOST=" + _routerIp;
        body += "&_InstID=" + instId;
        body += "&IPAddr=" + ipAddr;
        body += "&SubMask=" + subMask;
        body += "&SubnetMask=" + subnetMask;
        body += "&MinAddress=" + minAddress;
        body += "&MaxAddress=" + maxAddress;
        body += "&IPRouters=";
        body += "&DNSServer1=" + primDns;
        body += "&DNSServer2=" + secDns;
        body += "&LeaseTime=" + leaseTime;
        body += "&ServerEnable=" + serverEnable;
        for (int i = 0; i < 4; i++) body += "&sub_IPAddr" + String(i) + "=" + subIp[i];
        for (int i = 0; i < 4; i++) body += "&sub_SubMask" + String(i) + "=" + subSub[i];
        for (int i = 0; i < 4; i++) body += "&sub_MinAddress" + String(i) + "=" + subMin[i];
        for (int i = 0; i < 4; i++) body += "&sub_MaxAddress" + String(i) + "=" + subMax[i];
        body += "&DnsServerSource=0";
        for (int i = 0; i < 4; i++) body += "&sub_DNSServer1" + String(i) + "=" + subDns1[i];
        for (int i = 0; i < 4; i++) body += "&sub_DNSServer2" + String(i) + "=" + subDns2[i];
        body += "&Btn_cancel_DHCPBasicCfg=";
        body += "&Btn_apply_DHCPBasicCfg=";
        body += "&_sessionTOKEN=" + token;

        String bodyHash = CryptoHelpers::sha256Hex(body);
        String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

        HTTPClient http;
        http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=Localnet_LanMgrIpv4_DHCPBasicCfg_lua.lua");
        http.addHeader("Connection",       "close");
        http.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
        http.addHeader("X-Requested-With", "XMLHttpRequest");
        http.addHeader("Content-Type",     "application/x-www-form-urlencoded; charset=UTF-8");
        http.addHeader("Referer",          String("http://") + _routerIp + "/");
        http.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
        http.addHeader("Check",            checkHdr);
        http.setTimeout(10000);

        int code = http.POST(body);
        String resp = (code > 0) ? http.getString() : "";
        http.end();

        if (resp.indexOf("SessionTimeout") >= 0) {
            _loggedIn = false;
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        bool ok = (resp.indexOf("SUCC") >= 0 || resp.indexOf("<IF_ERRORTYPE>SUCC</IF_ERRORTYPE>") >= 0);
        Serial.printf("[ROUTER-DHCP] Apply DHCP DNS (%s / %s, DnsServerSource=0): code=%d ok=%d\n",
                      primDns.c_str(), secDns.c_str(), code, ok);
        if (ok) {
            _dnsSynced = true;
            _routerDnsPrimary = primDns;
            _routerDnsSecondary = secDns;
            _lastLog = "Router DHCP DNS Applied: " + primDns + " / " + secDns;
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return true;
        }
    }
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return false;
}

bool ZteRouterClient::registerRouterLocalDomain(const String& hostname, const String& ip) {
    if (_httpMutex) xSemaphoreTakeRecursive(_httpMutex, portMAX_DELAY);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!_loggedIn) relogin();
        if (!_loggedIn) {
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String token = getDnsContextToken();
        if (token.isEmpty()) {
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String currentXml = routerGET("/?_type=menuData&_tag=dns_hostname_lua.lua");
        if (currentXml.indexOf("SessionTimeout") >= 0) {
            _loggedIn = false;
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        String existingInstId = "-1";
        int hostIdx = currentXml.indexOf("<ParaValue>" + hostname + "</ParaValue>");
        if (hostIdx >= 0) {
            int instStart = currentXml.lastIndexOf("<Instance>", hostIdx);
            int instEnd = currentXml.indexOf("</Instance>", hostIdx);
            if (instStart >= 0 && instEnd > instStart) {
                String instXml = currentXml.substring(instStart, instEnd);
                if (instXml.indexOf("<ParaValue>" + ip + "</ParaValue>") >= 0) {
                    Serial.printf("[ROUTER-DNS] Local domain %s -> %s already registered in router.\n",
                                  hostname.c_str(), ip.c_str());
                    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
                    return true;
                }
                int pInst = instXml.indexOf("<ParaName>_InstID</ParaName>");
                if (pInst >= 0) {
                    int vStart = instXml.indexOf("<ParaValue>", pInst);
                    if (vStart >= 0) {
                        vStart += 11;
                        int vEnd = instXml.indexOf("</ParaValue>", vStart);
                        if (vEnd >= 0) existingInstId = instXml.substring(vStart, vEnd);
                    }
                }
            }
        }

        String body = "IF_ACTION=Apply&_InstID=" + existingInstId +
                      "&HostName=" + CryptoHelpers::urlEncode(hostname) +
                      "&IPAddress=" + CryptoHelpers::urlEncode(ip) +
                      "&_sessionTOKEN=" + token;
        String bodyHash = CryptoHelpers::sha256Hex(body);
        String checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

        HTTPClient http;
        http.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=dns_hostname_lua.lua");
        http.addHeader("Connection",       "close");
        http.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
        http.addHeader("X-Requested-With", "XMLHttpRequest");
        http.addHeader("Content-Type",     "application/x-www-form-urlencoded; charset=UTF-8");
        http.addHeader("Referer",          String("http://") + _routerIp + "/");
        http.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
        http.addHeader("Check",            checkHdr);
        http.setTimeout(10000);

        int code = http.POST(body);
        String resp = (code > 0) ? http.getString() : "";
        http.end();

        if (resp.indexOf("SessionTimeout") >= 0) {
            _loggedIn = false;
            if (attempt == 0) { relogin(); continue; }
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return false;
        }

        bool ok = (resp.indexOf("SUCC") >= 0);
        if (!ok && existingInstId == "-1") {
            token = getDnsContextToken();
            body = "IF_ACTION=Add&_InstID=-1&HostName=" + CryptoHelpers::urlEncode(hostname) +
                   "&IPAddress=" + CryptoHelpers::urlEncode(ip) +
                   "&_sessionTOKEN=" + token;
            bodyHash = CryptoHelpers::sha256Hex(body);
            checkHdr = CryptoHelpers::rsaEncryptBase64(bodyHash);

            HTTPClient http2;
            http2.begin(String("http://") + _routerIp + "/?_type=menuData&_tag=dns_hostname_lua.lua");
            http2.addHeader("Connection",       "close");
            http2.addHeader("User-Agent",       "Mozilla/5.0 (ESP32)");
            http2.addHeader("X-Requested-With", "XMLHttpRequest");
            http2.addHeader("Content-Type",     "application/x-www-form-urlencoded; charset=UTF-8");
            http2.addHeader("Referer",          String("http://") + _routerIp + "/");
            http2.addHeader("Cookie",           "SID=" + _sidCookie + "; _TESTCOOKIESUPPORT=1");
            http2.addHeader("Check",            checkHdr);
            http2.setTimeout(10000);
            code = http2.POST(body);
            resp = (code > 0) ? http2.getString() : "";
            http2.end();
            ok = (resp.indexOf("SUCC") >= 0);
        }

        Serial.printf("[ROUTER-DNS] Register local domain %s -> %s: code=%d ok=%d\n",
                      hostname.c_str(), ip.c_str(), code, ok);
        if (ok) {
            if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
            return true;
        }
    }
    if (_httpMutex) xSemaphoreGiveRecursive(_httpMutex);
    return false;
}

bool ZteRouterClient::syncRouterDnsProfile(const String& profileKey, const String& customPrimary, const String& customSecondary, bool highAvailability) {
    String p = profileKey;
    p.trim();
    p.toLowerCase();

    String primary = "1.1.1.1";
    String secondary = "1.0.0.1";
    String canonicalProf = "ultra_fast";

    if (p == "ultra_fast" || p == "cloudflare" || p == "fast" || p == "0") {
        canonicalProf = "ultra_fast";
        primary = "1.1.1.1";
        secondary = "1.0.0.1";
    } else if (p == "adguard" || p == "adblock" || p == "1") {
        canonicalProf = "adguard";
        primary = "94.140.14.14";
        secondary = "94.140.14.15";
    } else if (p == "family" || p == "familysafe" || p == "cloudflare_family" || p == "2") {
        canonicalProf = "family";
        primary = "1.1.1.3";
        secondary = "1.0.0.3";
    } else if (p == "google" || p == "google_dns") {
        canonicalProf = "google";
        primary = "8.8.8.8";
        secondary = "8.8.4.4";
    } else if (p == "custom" || p == "nextdns" || p == "3") {
        canonicalProf = "custom";
        primary = customPrimary.length() > 0 ? customPrimary : "1.1.1.1";
        secondary = customSecondary.length() > 0 ? customSecondary : "1.0.0.1";
    }

    _haMode = highAvailability;
    String espIp = WiFi.localIP().toString();
    if (espIp == "0.0.0.0" || espIp.length() == 0) espIp = "192.168.1.7";

    // High-Availability Hybrid DNS Architecture:
    // Option 6 Primary = ESP32 IP
    // Option 6 Secondary = Upstream (if HA mode) or 0.0.0.0 (if Strict mode)
    String dhcpSecondary = highAvailability ? primary : "0.0.0.0";

    applyRouterDns(primary, secondary);
    bool okDhcp = applyRouterDhcpDns(espIp, dhcpSecondary);

    _lastSyncedIp = espIp;
    _dnsSynced = okDhcp;

    if (canonicalProf == "custom") {
        DNSEngine::setCustomUpstreams(primary, secondary);
    } else {
        DNSEngine::setProfile(canonicalProf);
    }

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putString("dns_prof", canonicalProf);
    prefs.putBool("dns_ha", highAvailability);
    if (canonicalProf == "custom") {
        prefs.putString("dns_cp", primary);
        prefs.putString("dns_cs", secondary);
    }
    prefs.end();

    return okDhcp;
}

void ZteRouterClient::syncRouterDnsAtBoot() {
    Serial.println("[ROUTER-DNS] Boot synchronization starting...");
    String ip = WiFi.localIP().toString();
    if (ip == "0.0.0.0" || ip.length() == 0) ip = "192.168.1.7";

    // 1. Register ESP32 active IP as portal.home & antigravity.home in router local DNS
    registerRouterLocalDomain("portal.home", ip);
    registerRouterLocalDomain("antigravity.home", ip);

    // 2. Read saved profile from NVS
    Preferences prefs;
    prefs.begin("microrouter", true);
    String prof = prefs.getString("dns_prof", "ultra_fast");
    bool ha = prefs.getBool("dns_ha", true);
    String cp = prefs.getString("dns_cp", "");
    String cs = prefs.getString("dns_cs", "");
    prefs.end();

    // 3. Apply configured DNS profile to router (Hybrid HA DHCP distribution)
    syncRouterDnsProfile(prof, cp, cs, ha);
    _dnsBootSynced = true;
    _lastSyncedIp = ip;
}

bool ZteRouterClient::syncDns(const String& primaryDns, const String& secondaryDns) {
    return applyRouterDhcpDns(primaryDns, secondaryDns);
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
        if (!_dnsBootSynced && _loggedIn) {
            syncRouterDnsAtBoot();
        }

        // Dynamic IP Watchdog: check if local IP changed
        String currentIp = WiFi.localIP().toString();
        if (_dnsBootSynced && currentIp != "0.0.0.0" && _lastSyncedIp.length() > 0 && currentIp != _lastSyncedIp) {
            Serial.printf("[ROUTER-DNS] IP drift detected! Old: %s, New: %s. Re-syncing...\n",
                          _lastSyncedIp.c_str(), currentIp.c_str());
            syncRouterDnsAtBoot();
        }

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

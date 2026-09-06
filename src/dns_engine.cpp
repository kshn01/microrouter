#include "dns_engine.h"
#include "device_manager.h"
#include "config.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
#include <errno.h>
#include <string.h>

DNSEngine dnsEngine;

// ─── Supported Upstream Profiles ──────────────────────────────────
static const DnsProfileDef DNS_PROFILES[] = {
    { DNS_PROF_ULTRA_FAST, "ultra_fast", "Ultra-Fast (Cloudflare)",  "1.1.1.1",      "1.0.0.1" },
    { DNS_PROF_ADGUARD,    "adguard",    "AdGuard (Ad-Blocking)",    "94.140.14.14", "94.140.14.15" },
    { DNS_PROF_FAMILY,     "family",     "Family Safe (Cloudflare)", "1.1.1.3",      "1.0.0.3" }
};
static const size_t NUM_PROFILES = sizeof(DNS_PROFILES) / sizeof(DNS_PROFILES[0]);

// ─── Runtime State ────────────────────────────────────────────────
static SemaphoreHandle_t s_dnsMutex          = nullptr;
static bool              s_dnsEnabled        = true;
static uint8_t           s_activeProfileId   = DNS_PROF_ULTRA_FAST;
static char              s_activeProfileKey[16]  = "ultra_fast";
static char              s_activeProfileName[32] = "Ultra-Fast (Cloudflare)";
static char              s_upstreamPrimary[16]   = "1.1.1.1";
static char              s_upstreamSecondary[16] = "1.0.0.1";

static uint32_t s_dnsStartTime          = 0;
static uint32_t s_totalQueries          = 0;
static uint32_t s_queriesAnswered       = 0;
static uint32_t s_queriesForwarded      = 0;
static uint32_t s_queriesBlocked        = 0;
static uint32_t s_localInterceptCount   = 0;
static uint64_t s_totalLatencyMs        = 0;

// ─── Focus & Content Shield ───────────────────────────────────────
static bool s_blockMeta = false;
static bool s_blockTiktok = false;
static char s_customBlockedDomains[512] = "";

static const char* const META_DOMAINS[] = {
    "instagram.com", "cdninstagram.com", "facebook.com", "fbcdn.net",
    "fbsbx.com", "fb.com", "fb.me", "meta.com", "threads.net", "messenger.com"
};
static const size_t META_DOMAINS_COUNT = sizeof(META_DOMAINS) / sizeof(META_DOMAINS[0]);

static const char* const TIKTOK_DOMAINS[] = {
    "tiktok.com", "tiktokv.com", "tiktokcdn.com", "byteoversea.com", "ibytedtos.com"
};
static const size_t TIKTOK_DOMAINS_COUNT = sizeof(TIKTOK_DOMAINS) / sizeof(TIKTOK_DOMAINS[0]);

// ─── Network Spyglass: Circular Query Ring Buffer ──────────────────
static DnsQueryRecord s_queryLog[DNS_QUERY_LOG_SIZE];
static uint16_t       s_queryLogHead  = 0;
static uint16_t       s_queryLogCount = 0;

// ─── Wire Buffers in Static BSS (Zero Stack / Zero Heap) ───────────
static uint8_t s_rxBuf[512];
static uint8_t s_txBuf[512];

static TaskHandle_t s_dnsTaskHandle = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────

static inline bool domainMatches(const char* domain, const char* pattern) {
    if (!domain || !pattern) return false;
    if (strcasecmp(domain, pattern) == 0) return true;
    size_t dLen = strlen(domain);
    size_t pLen = strlen(pattern);
    if (dLen > pLen + 1 && domain[dLen - pLen - 1] == '.') {
        if (strcasecmp(domain + dLen - pLen, pattern) == 0) return true;
    }
    return false;
}

static bool isDomainBlockedByPolicy(const char* domain) {
    if (!domain || domain[0] == '\0') return false;

    bool blockMeta = false;
    bool blockTiktok = false;
    char customBuf[512];
    customBuf[0] = '\0';

    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    blockMeta = s_blockMeta;
    blockTiktok = s_blockTiktok;
    strncpy(customBuf, s_customBlockedDomains, sizeof(customBuf) - 1);
    customBuf[sizeof(customBuf) - 1] = '\0';
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

    if (blockMeta) {
        for (size_t i = 0; i < META_DOMAINS_COUNT; i++) {
            if (domainMatches(domain, META_DOMAINS[i])) return true;
        }
    }

    if (blockTiktok) {
        for (size_t i = 0; i < TIKTOK_DOMAINS_COUNT; i++) {
            if (domainMatches(domain, TIKTOK_DOMAINS[i])) return true;
        }
    }

    if (customBuf[0] != '\0') {
        char* cur = customBuf;
        while (*cur) {
            while (*cur == ' ' || *cur == ',') cur++;
            if (*cur == '\0') break;
            char* start = cur;
            while (*cur && *cur != ',') cur++;
            char saved = *cur;
            *cur = '\0';
            char* end = cur - 1;
            while (end > start && *end == ' ') { *end = '\0'; end--; }
            if (strlen(start) > 0 && start[0] != '#' && start[0] != '!') {
                if (domainMatches(domain, start)) return true;
            }
            if (saved == '\0') break;
            cur++;
        }
    }

    return false;
}

static inline bool isLocalDomain(const char* domain) {
    if (!domain) return false;
    if (strcasecmp(domain, "microrouter.local") == 0 ||
        strcasecmp(domain, "portal.home") == 0 ||
        strcasecmp(domain, "antigravity.home") == 0) return true;
    size_t len = strlen(domain);
    if (len > 12 && strcasecmp(domain + len - 12, ".portal.home") == 0) return true;
    return false;
}

static inline bool isDoHCanaryDomain(const char* domain) {
    if (!domain) return false;
    return strcasecmp(domain, "use-application-dns.net") == 0;
}

static void logQuery(const char* domain, const char* clientIp, uint8_t status, uint16_t latencyMs, uint8_t qType) {
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);

    DnsQueryRecord* rec = &s_queryLog[s_queryLogHead];
    strncpy(rec->domain, domain ? domain : "", sizeof(rec->domain) - 1);
    rec->domain[sizeof(rec->domain) - 1] = '\0';
    strncpy(rec->clientIp, clientIp ? clientIp : "", sizeof(rec->clientIp) - 1);
    rec->clientIp[sizeof(rec->clientIp) - 1] = '\0';
    rec->latencyMs = latencyMs;
    rec->timestamp = millis();
    rec->status    = status;
    rec->qType     = qType;

    s_queryLogHead = (s_queryLogHead + 1) % DNS_QUERY_LOG_SIZE;
    if (s_queryLogCount < DNS_QUERY_LOG_SIZE) s_queryLogCount++;

    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

    // Actively register client in DeviceManager for real-time inventory
    if (clientIp && clientIp[0] != '\0') {
        deviceManager.registerClientActivity(clientIp, domain);
    }
}

// ─── Zero-Heap RFC 1035 Question Parser ───────────────────────────
static int parseQuestion(const uint8_t* buf, int len, char* outDomain, size_t outDomainMax, uint16_t* outType, uint16_t* outClass) {
    if (len < 12) return -1;
    int ptr = 12;
    size_t outPtr = 0;

    while (ptr < len) {
        uint8_t labelLen = buf[ptr++];
        if (labelLen == 0) break;
        if ((labelLen & 0xC0) != 0) return -1; // Pointer not valid in Question QNAME
        if (ptr + labelLen > len) return -1;

        if (outPtr > 0 && outPtr < outDomainMax - 1) {
            outDomain[outPtr++] = '.';
        }
        for (uint8_t i = 0; i < labelLen; i++) {
            if (outPtr < outDomainMax - 1) {
                char c = (char)buf[ptr++];
                if (c >= 'A' && c <= 'Z') c += ('a' - 'A'); // Lowercase
                outDomain[outPtr++] = c;
            } else {
                ptr++;
            }
        }
    }

    if (outPtr < outDomainMax) {
        outDomain[outPtr] = '\0';
    } else {
        outDomain[outDomainMax - 1] = '\0';
    }

    if (ptr + 4 > len) return -1;
    *outType  = ((uint16_t)buf[ptr]     << 8) | buf[ptr + 1];
    *outClass = ((uint16_t)buf[ptr + 2] << 8) | buf[ptr + 3];
    return ptr + 4;
}

// ─── FreeRTOS Dedicated UDP Port 53 Server Task ───────────────────
static void dnsProxyTask(void* pvParameters) {
    int serverSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSock < 0) {
        Serial.printf("[DNS] Failed to create server socket: %d\n", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port        = htons(53);

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        Serial.printf("[DNS] Bind failed on port 53: %d\n", errno);
        close(serverSock);
        vTaskDelete(NULL);
        return;
    }

    // Upstream client socket
    int upstreamSock = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval upTimeout;
    upTimeout.tv_sec  = 1;
    upTimeout.tv_usec = 200000; // 1.2s timeout
    if (upstreamSock >= 0) {
        setsockopt(upstreamSock, SOL_SOCKET, SO_RCVTIMEO, &upTimeout, sizeof(upTimeout));
    }

    Serial.println("[DNS] Port 53 server listening on UDP");

    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int rxLen = recvfrom(serverSock, s_rxBuf, sizeof(s_rxBuf), 0,
                             (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (rxLen < 12) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char clientIpStr[16] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, sizeof(clientIpStr));

        // Must be standard query (QR=0, Opcode=0)
        uint8_t flagsHigh = s_rxBuf[2];
        if ((flagsHigh & 0x78) != 0) continue;

        char qDomain[64] = {0};
        uint16_t qType = 0;
        uint16_t qClass = 0;
        int qEnd = parseQuestion(s_rxBuf, rxLen, qDomain, sizeof(qDomain), &qType, &qClass);
        if (qEnd < 0) continue;

        // 1. Local Domain Synthesis (microrouter.local / portal.home)
        if (isLocalDomain(qDomain)) {
            if (qType == 1 && qClass == 1) { // Type A
                memcpy(s_txBuf, s_rxBuf, qEnd);
                s_txBuf[2] = 0x85; // QR=1, AA=1, RD=1
                s_txBuf[3] = 0x80; // RA=1, RCODE=0
                s_txBuf[6] = 0x00; s_txBuf[7] = 0x01; // ANCOUNT = 1
                s_txBuf[8] = 0x00; s_txBuf[9] = 0x00;
                s_txBuf[10] = 0x00; s_txBuf[11] = 0x00;

                int p = qEnd;
                s_txBuf[p++] = 0xC0; s_txBuf[p++] = 0x0C; // Pointer to QNAME
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x01; // TYPE A
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x01; // CLASS IN
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x3C; // TTL 60s
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x04; // RDLENGTH 4

                IPAddress localIp = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
                s_txBuf[p++] = localIp[0];
                s_txBuf[p++] = localIp[1];
                s_txBuf[p++] = localIp[2];
                s_txBuf[p++] = localIp[3];

                sendto(serverSock, s_txBuf, p, 0, (struct sockaddr*)&clientAddr, clientAddrLen);

                if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
                s_totalQueries++;
                s_queriesAnswered++;
                s_localInterceptCount++;
                if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

                logQuery(qDomain, clientIpStr, DNS_STATUS_LOCAL, 0, qType);
                continue;
            }
        }

        // 2. DoH Canary Sinkhole (RFC/Mozilla use-application-dns.net -> NXDOMAIN)
        if (isDoHCanaryDomain(qDomain)) {
            memcpy(s_txBuf, s_rxBuf, qEnd);
            s_txBuf[2] = 0x85; // QR=1, AA=1, RD=1
            s_txBuf[3] = 0x83; // RA=1, RCODE=3 (NXDOMAIN)
            s_txBuf[6] = 0x00; s_txBuf[7] = 0x00;
            s_txBuf[8] = 0x00; s_txBuf[9] = 0x00;
            s_txBuf[10] = 0x00; s_txBuf[11] = 0x00;

            sendto(serverSock, s_txBuf, qEnd, 0, (struct sockaddr*)&clientAddr, clientAddrLen);

            if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
            s_totalQueries++;
            s_queriesAnswered++;
            s_localInterceptCount++;
            if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

            logQuery(qDomain, clientIpStr, DNS_STATUS_CANARY, 0, qType);
            continue;
        }

        // 3. Focus & Content Shield: Sinkhole (0.0.0.0)
        if (isDomainBlockedByPolicy(qDomain)) {
            memcpy(s_txBuf, s_rxBuf, qEnd);
            s_txBuf[2] = 0x85; // QR=1, AA=1, RD=1
            s_txBuf[3] = 0x80; // RA=1, RCODE=0 (NoError)
            s_txBuf[6] = 0x00; s_txBuf[7] = (qType == 1 && qClass == 1) ? 0x01 : 0x00;
            s_txBuf[8] = 0x00; s_txBuf[9] = 0x00;
            s_txBuf[10] = 0x00; s_txBuf[11] = 0x00;

            int p = qEnd;
            if (qType == 1 && qClass == 1) {
                s_txBuf[p++] = 0xC0; s_txBuf[p++] = 0x0C;
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x01;
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x01;
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x3C;
                s_txBuf[p++] = 0x00; s_txBuf[p++] = 0x04;
                s_txBuf[p++] = 0; s_txBuf[p++] = 0; s_txBuf[p++] = 0; s_txBuf[p++] = 0; // 0.0.0.0
            }

            sendto(serverSock, s_txBuf, p, 0, (struct sockaddr*)&clientAddr, clientAddrLen);

            if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
            s_totalQueries++;
            s_queriesAnswered++;
            s_queriesBlocked++;
            if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

            logQuery(qDomain, clientIpStr, DNS_STATUS_BLOCKED, 0, qType);
            continue;
        }

        // 4. Upstream Forwarding
        char primaryUp[16] = {0};
        char secondaryUp[16] = {0};
        if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
        strncpy(primaryUp, s_upstreamPrimary, sizeof(primaryUp) - 1);
        strncpy(secondaryUp, s_upstreamSecondary, sizeof(secondaryUp) - 1);
        if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

        unsigned long t0 = millis();
        bool answered = false;

        if (upstreamSock >= 0) {
            struct sockaddr_in upAddr;
            memset(&upAddr, 0, sizeof(upAddr));
            upAddr.sin_family = AF_INET;
            upAddr.sin_port   = htons(53);
            inet_pton(AF_INET, primaryUp, &upAddr.sin_addr);

            sendto(upstreamSock, s_rxBuf, rxLen, 0, (struct sockaddr*)&upAddr, sizeof(upAddr));

            struct sockaddr_in respAddr;
            socklen_t respAddrLen = sizeof(respAddr);
            int n = recvfrom(upstreamSock, s_txBuf, sizeof(s_txBuf), 0, (struct sockaddr*)&respAddr, &respAddrLen);

            if (n >= 12) {
                sendto(serverSock, s_txBuf, n, 0, (struct sockaddr*)&clientAddr, clientAddrLen);
                answered = true;
            } else if (strlen(secondaryUp) > 0 && strcmp(secondaryUp, "0.0.0.0") != 0) {
                // Try secondary upstream
                inet_pton(AF_INET, secondaryUp, &upAddr.sin_addr);
                sendto(upstreamSock, s_rxBuf, rxLen, 0, (struct sockaddr*)&upAddr, sizeof(upAddr));
                n = recvfrom(upstreamSock, s_txBuf, sizeof(s_txBuf), 0, (struct sockaddr*)&respAddr, &respAddrLen);
                if (n >= 12) {
                    sendto(serverSock, s_txBuf, n, 0, (struct sockaddr*)&clientAddr, clientAddrLen);
                    answered = true;
                }
            }
        }

        uint16_t latency = (uint16_t)(millis() - t0);

        if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
        s_totalQueries++;
        if (answered) {
            s_queriesAnswered++;
            s_queriesForwarded++;
            s_totalLatencyMs += latency;
        }
        if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

        logQuery(qDomain, clientIpStr, answered ? DNS_STATUS_RESOLVED : DNS_STATUS_SERVFAIL, latency, qType);
    }
}

// ─── Public API Implementations ───────────────────────────────────

void DNSEngine::begin() {
    if (!s_dnsMutex) {
        s_dnsMutex = xSemaphoreCreateMutex();
    }

    Preferences prefs;
    prefs.begin("microrouter", true);
    String savedProf = prefs.getString("dns_profile", "ultra_fast");
    s_blockMeta = prefs.getBool("dns_blk_meta", false);
    s_blockTiktok = prefs.getBool("dns_blk_tiktok", false);
    String savedCustom = prefs.getString("dns_blk_custom", "");
    strncpy(s_customBlockedDomains, savedCustom.c_str(), sizeof(s_customBlockedDomains) - 1);
    prefs.end();

    setProfile(savedProf);

    s_dnsStartTime = millis();

    xTaskCreatePinnedToCore(
        dnsProxyTask,
        "dnsProxyTask",
        4096,
        nullptr,
        2,
        &s_dnsTaskHandle,
        0
    );

    Serial.printf("[DNS] DNS Shield initialized (Profile: %s [%s])\n", s_activeProfileName, s_upstreamPrimary);
}

void DNSEngine::getStats(DnsStatsSnapshot* outSnap) {
    if (!outSnap) return;
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);

    outSnap->enabled = s_dnsEnabled;
    outSnap->profileId = s_activeProfileId;
    strncpy(outSnap->profileKey, s_activeProfileKey, sizeof(outSnap->profileKey) - 1);
    strncpy(outSnap->profileName, s_activeProfileName, sizeof(outSnap->profileName) - 1);
    strncpy(outSnap->upstreamPrimary, s_upstreamPrimary, sizeof(outSnap->upstreamPrimary) - 1);
    strncpy(outSnap->upstreamSecondary, s_upstreamSecondary, sizeof(outSnap->upstreamSecondary) - 1);
    outSnap->uptimeSecs = s_dnsStartTime > 0 ? (millis() - s_dnsStartTime) / 1000 : 0;
    outSnap->totalQueries = s_totalQueries;
    outSnap->queriesAnswered = s_queriesAnswered;
    outSnap->queriesForwarded = s_queriesForwarded;
    outSnap->queriesBlocked = s_queriesBlocked;
    outSnap->localInterceptCount = s_localInterceptCount;
    outSnap->avgLatencyMs = s_queriesForwarded > 0 ? (uint16_t)(s_totalLatencyMs / s_queriesForwarded) : 0;

    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);
}

bool DNSEngine::setProfile(const String& profileKey) {
    uint8_t profId = DNS_PROF_ULTRA_FAST;
    if (profileKey.equalsIgnoreCase("adguard")) profId = DNS_PROF_ADGUARD;
    else if (profileKey.equalsIgnoreCase("family")) profId = DNS_PROF_FAMILY;

    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    s_activeProfileId = profId;
    strncpy(s_activeProfileKey, DNS_PROFILES[profId].key, sizeof(s_activeProfileKey) - 1);
    strncpy(s_activeProfileName, DNS_PROFILES[profId].name, sizeof(s_activeProfileName) - 1);
    strncpy(s_upstreamPrimary, DNS_PROFILES[profId].primaryIp, sizeof(s_upstreamPrimary) - 1);
    strncpy(s_upstreamSecondary, DNS_PROFILES[profId].secondaryIp, sizeof(s_upstreamSecondary) - 1);
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putString("dns_profile", profileKey);
    prefs.end();

    return true;
}

bool DNSEngine::setCustomUpstreams(const String& primaryIp, const String& secondaryIp) {
    if (primaryIp.length() == 0) return false;

    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    s_activeProfileId = DNS_PROF_CUSTOM;
    strncpy(s_activeProfileKey, "custom", sizeof(s_activeProfileKey) - 1);
    strncpy(s_activeProfileName, "Custom / NextDNS", sizeof(s_activeProfileName) - 1);
    strncpy(s_upstreamPrimary, primaryIp.c_str(), sizeof(s_upstreamPrimary) - 1);
    strncpy(s_upstreamSecondary, secondaryIp.c_str(), sizeof(s_upstreamSecondary) - 1);
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putString("dns_profile", "custom");
    prefs.putString("dns_custom_p", primaryIp);
    prefs.putString("dns_custom_s", secondaryIp);
    prefs.end();

    return true;
}

String DNSEngine::getActiveProfileKey() {
    char k[16];
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    strncpy(k, s_activeProfileKey, sizeof(k) - 1);
    k[sizeof(k) - 1] = '\0';
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);
    return String(k);
}

void DNSEngine::getShieldRules(DnsShieldRules* outRules) {
    if (!outRules) return;
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    outRules->blockMeta = s_blockMeta;
    outRules->blockTiktok = s_blockTiktok;
    strncpy(outRules->customDomains, s_customBlockedDomains, sizeof(outRules->customDomains) - 1);
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);
}

bool DNSEngine::setShieldRules(bool blockMeta, bool blockTiktok, const String& customDomains) {
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    s_blockMeta = blockMeta;
    s_blockTiktok = blockTiktok;
    strncpy(s_customBlockedDomains, customDomains.c_str(), sizeof(s_customBlockedDomains) - 1);
    s_customBlockedDomains[sizeof(s_customBlockedDomains) - 1] = '\0';
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);

    Preferences prefs;
    prefs.begin("microrouter", false);
    prefs.putBool("dns_blk_meta", blockMeta);
    prefs.putBool("dns_blk_tiktok", blockTiktok);
    prefs.putString("dns_blk_custom", customDomains);
    prefs.end();

    return true;
}

uint16_t DNSEngine::getRecentQueries(DnsQueryRecord* outRecords, uint16_t maxRecords) {
    if (!outRecords || maxRecords == 0) return 0;
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);

    uint16_t count = (s_queryLogCount < maxRecords) ? s_queryLogCount : maxRecords;
    for (uint16_t i = 0; i < count; i++) {
        int idx = (s_queryLogHead - 1 - i + DNS_QUERY_LOG_SIZE) % DNS_QUERY_LOG_SIZE;
        outRecords[i] = s_queryLog[idx];
    }

    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);
    return count;
}

String DNSEngine::getRecentQueriesJson() {
    JsonDocument doc;
    JsonArray queries = doc["queries"].to<JsonArray>();

    DnsQueryRecord records[DNS_QUERY_LOG_SIZE];
    uint16_t count = getRecentQueries(records, DNS_QUERY_LOG_SIZE);

    for (uint16_t i = 0; i < count; i++) {
        JsonObject q = queries.add<JsonObject>();
        q["domain"]    = records[i].domain;
        q["clientIp"]  = records[i].clientIp;
        q["latencyMs"] = records[i].latencyMs;
        q["timestamp"] = records[i].timestamp;
        q["status"]    = statusToString(records[i].status);
        q["blocked"]   = (records[i].status == DNS_STATUS_BLOCKED || records[i].status == DNS_STATUS_CANARY);
        q["reason"]    = statusToString(records[i].status);
        q["qType"]     = records[i].qType == 28 ? "AAAA" : "A";
    }

    DnsStatsSnapshot snap;
    getStats(&snap);
    JsonObject stats = doc["stats"].to<JsonObject>();
    stats["total"]     = snap.totalQueries;
    stats["answered"]  = snap.queriesAnswered;
    stats["forwarded"] = snap.queriesForwarded;
    stats["blocked"]   = snap.queriesBlocked;
    stats["local"]     = snap.localInterceptCount;
    stats["avgLatency"]= snap.avgLatencyMs;
    stats["profile"]   = snap.profileKey;
    stats["profileName"] = snap.profileName;

    String json;
    serializeJson(doc, json);
    return json;
}

void DNSEngine::clearQueryLog() {
    if (s_dnsMutex) xSemaphoreTake(s_dnsMutex, portMAX_DELAY);
    s_queryLogHead = 0;
    s_queryLogCount = 0;
    if (s_dnsMutex) xSemaphoreGive(s_dnsMutex);
}

const char* DNSEngine::statusToString(uint8_t status) {
    switch (status) {
        case DNS_STATUS_RESOLVED: return "RESOLVED";
        case DNS_STATUS_BLOCKED:  return "BLOCKED";
        case DNS_STATUS_LOCAL:    return "LOCAL";
        case DNS_STATUS_CANARY:   return "CANARY";
        case DNS_STATUS_SERVFAIL: return "SERVFAIL";
        default:                  return "UNKNOWN";
    }
}

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  DNSEngine — Asynchronous UDP Port 53 DNS Shield Proxy      ║
// ║  Features: Upstream Switcher, DoH Canary, Local Synthesis,   ║
// ║  Focus Content Shield, and Network Spyglass Ring Buffer      ║
// ╚══════════════════════════════════════════════════════════════╝

enum DnsProfileId : uint8_t {
    DNS_PROF_ULTRA_FAST = 0,    // Cloudflare 1.1.1.1 / 1.0.0.1
    DNS_PROF_ADGUARD    = 1,    // AdGuard 94.140.14.14 / 94.140.14.15
    DNS_PROF_FAMILY     = 2,    // Cloudflare Family 1.1.1.3 / 1.0.0.3
    DNS_PROF_CUSTOM     = 3     // Custom User Upstream
};

struct DnsProfileDef {
    uint8_t id;
    const char* key;
    const char* name;
    const char* primaryIp;
    const char* secondaryIp;
};

enum DnsQueryStatus : uint8_t {
    DNS_STATUS_RESOLVED = 0,
    DNS_STATUS_BLOCKED  = 1,
    DNS_STATUS_LOCAL    = 2,
    DNS_STATUS_CANARY   = 3,
    DNS_STATUS_SERVFAIL = 4
};

#define DNS_QUERY_LOG_SIZE 40

struct DnsQueryRecord {
    char     domain[64];
    char     clientIp[16];
    uint16_t latencyMs;
    uint32_t timestamp;
    uint8_t  status;   // DnsQueryStatus
    uint8_t  qType;    // RFC 1035 TYPE (A=1, AAAA=28)
};

struct DnsStatsSnapshot {
    bool     enabled;
    uint8_t  profileId;
    char     profileKey[16];
    char     profileName[32];
    char     upstreamPrimary[16];
    char     upstreamSecondary[16];
    uint32_t uptimeSecs;
    uint32_t totalQueries;
    uint32_t queriesAnswered;
    uint32_t queriesForwarded;
    uint32_t queriesBlocked;
    uint32_t localInterceptCount;
    uint16_t avgLatencyMs;
};

struct DnsShieldRules {
    bool blockMeta;
    bool blockTiktok;
    char customDomains[512];
};

class DNSEngine {
public:
    static void begin();
    static void getStats(DnsStatsSnapshot* outSnap);
    static bool setProfile(const String& profileKey);
    static bool setCustomUpstreams(const String& primaryIp, const String& secondaryIp);
    static String getActiveProfileKey();

    static void getShieldRules(DnsShieldRules* outRules);
    static bool setShieldRules(bool blockMeta, bool blockTiktok, const String& customDomains);

    static uint16_t getRecentQueries(DnsQueryRecord* outRecords, uint16_t maxRecords);
    static String getRecentQueriesJson();
    static void clearQueryLog();

    static const char* statusToString(uint8_t status);
};

extern DNSEngine dnsEngine;

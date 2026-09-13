#pragma once

#include <stdint.h>
#include <stdbool.h>

// ╔══════════════════════════════════════════════════════════════╗
// ║  RestrictionPolicy — Pure Domain Rules for Client Control    ║
// ╚══════════════════════════════════════════════════════════════╝

struct ClientRestrictionInput {
    bool isBlocked;
    bool isParentalTarget;
    bool hasActiveWaiver;
    bool isCurfewActive;
    bool hourlyQuotaEnabled;
    bool dailyQuotaEnabled;
    uint64_t hourlyUsageBytes;
    uint64_t hourlyLimitBytes;
    uint64_t dailyUsageBytes;
    uint64_t dailyLimitBytes;
};

/**
 * Deterministically evaluate whether a client station's traffic/DNS
 * should be restricted based on administrative blocklist, active emergency waiver,
 * parental control enrollment, curfew schedule, and bandwidth quotas.
 */
inline bool evaluateClientRestriction(const ClientRestrictionInput& in) {
    // 1. Hard administrative block always takes highest priority
    if (in.isBlocked) {
        return true;
    }

    // 2. Active temporary emergency waiver bypasses all curfew and quota limits
    if (in.hasActiveWaiver) {
        return false;
    }

    // 3. If station is not an enrolled parental target (Guest / designated device),
    // neither curfew nor quotas restrict its traffic
    if (!in.isParentalTarget) {
        return false;
    }

    // 4. Curfew window enforcement
    if (in.isCurfewActive) {
        return true;
    }

    // 5. Bandwidth quota limits (hourly throttle and daily cap)
    if (in.hourlyQuotaEnabled && in.hourlyUsageBytes >= in.hourlyLimitBytes) {
        return true;
    }
    if (in.dailyQuotaEnabled && in.dailyUsageBytes >= in.dailyLimitBytes) {
        return true;
    }

    return false;
}

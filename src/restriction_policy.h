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

enum ClientRestrictionReason : uint8_t {
    RESTRICTION_NONE = 0,
    RESTRICTION_ADMIN_BLOCK = 1,
    RESTRICTION_CURFEW = 2,
    RESTRICTION_QUOTA_HOURLY = 3,
    RESTRICTION_QUOTA_DAILY = 4
};

/**
 * Detailed evaluation returning the exact reason for client restriction.
 */
inline ClientRestrictionReason evaluateClientRestrictionDetail(const ClientRestrictionInput& in) {
    // 1. Hard administrative block always takes highest priority
    if (in.isBlocked) {
        return RESTRICTION_ADMIN_BLOCK;
    }

    // 2. Active temporary emergency waiver bypasses all curfew and quota limits
    if (in.hasActiveWaiver) {
        return RESTRICTION_NONE;
    }

    // 3. If station is not an enrolled parental target, neither curfew nor quotas apply
    if (!in.isParentalTarget) {
        return RESTRICTION_NONE;
    }

    // 4. Curfew window enforcement
    if (in.isCurfewActive) {
        return RESTRICTION_CURFEW;
    }

    // 5. Bandwidth quota limits (hourly throttle and daily cap)
    if (in.hourlyQuotaEnabled && in.hourlyUsageBytes >= in.hourlyLimitBytes) {
        return RESTRICTION_QUOTA_HOURLY;
    }
    if (in.dailyQuotaEnabled && in.dailyUsageBytes >= in.dailyLimitBytes) {
        return RESTRICTION_QUOTA_DAILY;
    }

    return RESTRICTION_NONE;
}

/**
 * Deterministically evaluate whether a client station's traffic/DNS
 * should be restricted based on administrative blocklist, active emergency waiver,
 * parental control enrollment, curfew schedule, and bandwidth quotas.
 */
inline bool evaluateClientRestriction(const ClientRestrictionInput& in) {
    return evaluateClientRestrictionDetail(in) != RESTRICTION_NONE;
}

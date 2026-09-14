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

    // 5. Bandwidth quota limits (daily cap takes precedence over hourly throttle)
    if (in.dailyQuotaEnabled && in.dailyLimitBytes > 0 && in.dailyUsageBytes >= in.dailyLimitBytes) {
        return RESTRICTION_QUOTA_DAILY;
    }
    if (in.hourlyQuotaEnabled && in.hourlyLimitBytes > 0 && in.hourlyUsageBytes >= in.hourlyLimitBytes) {
        return RESTRICTION_QUOTA_HOURLY;
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

/**
 * Pure domain policy for resolving whether a client device is enrolled in parental controls:
 * 1. If admin explicitly disabled parental control for this MAC -> false.
 * 2. If admin explicitly enabled parental control for this MAC -> true.
 * 3. Default for newly connected device: true if connection is a Guest connection (SoftAP / 192.168.4.x), false otherwise.
 */
inline bool resolveParentalEnrollment(bool explicitlyDisabled, bool explicitlyEnabled, bool isGuestConnection) {
    if (explicitlyDisabled) {
        return false;
    }
    if (explicitlyEnabled) {
        return true;
    }
    return isGuestConnection;
}

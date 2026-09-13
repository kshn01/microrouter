#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Pure, zero-allocation sliding token bucket rate limiter for embedded systems.
 * Prevents API request floods and FreeRTOS task socket exhaustion on ESP32.
 */
class RateLimiter {
public:
    static constexpr size_t MAX_TRACKED_IPS = 16;
    static constexpr uint8_t DEFAULT_BURST_CAPACITY = 25;   // Max tokens (burst)
    static constexpr uint8_t DEFAULT_REFILL_PER_SEC = 10;   // Tokens replenished per second

    struct ClientBucket {
        uint32_t ip;
        uint32_t lastRefillMs;
        uint8_t  tokens;
        uint32_t lastSeenMs;
    };

    explicit RateLimiter(uint8_t burstCapacity = DEFAULT_BURST_CAPACITY,
                         uint8_t refillPerSec = DEFAULT_REFILL_PER_SEC)
        : _burstCapacity(burstCapacity), _refillPerSec(refillPerSec) {
        reset();
    }

    void reset() {
        for (size_t i = 0; i < MAX_TRACKED_IPS; i++) {
            _buckets[i].ip = 0;
            _buckets[i].lastRefillMs = 0;
            _buckets[i].tokens = _burstCapacity;
            _buckets[i].lastSeenMs = 0;
        }
    }

    /**
     * Check whether a request from the given IP is allowed at the current timestamp.
     * Returns true if allowed, false if rate limited.
     */
    bool allow(uint32_t ip, uint32_t nowMs) {
        if (ip == 0) return true; // Local/internal requests always pass

        ClientBucket* b = _findOrCreateBucket(ip, nowMs);
        if (!b) return true; // Fail-open if table is full

        // Refill tokens based on elapsed time
        _refillBucket(*b, nowMs);
        b->lastSeenMs = nowMs;

        if (b->tokens > 0) {
            b->tokens--;
            return true;
        }

        return false;
    }

    uint8_t getTokens(uint32_t ip, uint32_t nowMs) {
        ClientBucket* b = _findOrCreateBucket(ip, nowMs);
        if (!b) return _burstCapacity;
        _refillBucket(*b, nowMs);
        return b->tokens;
    }

private:
    uint8_t _burstCapacity;
    uint8_t _refillPerSec;
    ClientBucket _buckets[MAX_TRACKED_IPS];

    void _refillBucket(ClientBucket& b, uint32_t nowMs) const {
        if (nowMs < b.lastRefillMs) {
            // Clock wrap-around or reset
            b.lastRefillMs = nowMs;
            return;
        }

        uint32_t elapsedMs = nowMs - b.lastRefillMs;
        if (elapsedMs >= 100) {
            uint32_t newTokens = (elapsedMs * _refillPerSec) / 1000;
            if (newTokens > 0) {
                b.tokens = (uint8_t)((b.tokens + newTokens > _burstCapacity) ? _burstCapacity : (b.tokens + newTokens));
                b.lastRefillMs = nowMs;
            }
        }
    }

    ClientBucket* _findOrCreateBucket(uint32_t ip, uint32_t nowMs) {
        // 1. Find existing
        for (size_t i = 0; i < MAX_TRACKED_IPS; i++) {
            if (_buckets[i].ip == ip) {
                return &_buckets[i];
            }
        }

        // 2. Find empty slot
        for (size_t i = 0; i < MAX_TRACKED_IPS; i++) {
            if (_buckets[i].ip == 0) {
                _buckets[i].ip = ip;
                _buckets[i].lastRefillMs = nowMs;
                _buckets[i].tokens = _burstCapacity;
                _buckets[i].lastSeenMs = nowMs;
                return &_buckets[i];
            }
        }

        // 3. Find LRU slot if all are occupied
        size_t lruIndex = 0;
        uint32_t oldestSeen = _buckets[0].lastSeenMs;
        for (size_t i = 1; i < MAX_TRACKED_IPS; i++) {
            if (_buckets[i].lastSeenMs < oldestSeen) {
                oldestSeen = _buckets[i].lastSeenMs;
                lruIndex = i;
            }
        }

        _buckets[lruIndex].ip = ip;
        _buckets[lruIndex].lastRefillMs = nowMs;
        _buckets[lruIndex].tokens = _burstCapacity;
        _buckets[lruIndex].lastSeenMs = nowMs;
        return &_buckets[lruIndex];
    }
};

#include <unity.h>

#include "dns_policy.h"
#include "rate_limiter.h"
#include "restriction_policy.h"
#include "scheduler_policy.h"
#include "usage_policy.h"
#include "wifi_scan_policy.h"

void test_disabled_schedule_is_inactive() {
    TEST_ASSERT_FALSE(isScheduleActive(false, true, 23 * 60, 22 * 60, 6 * 60));
}

void test_unsynced_clock_is_inactive() {
    TEST_ASSERT_FALSE(isScheduleActive(true, false, 23 * 60, 22 * 60, 6 * 60));
}

void test_overnight_schedule_covers_both_sides_of_midnight() {
    TEST_ASSERT_TRUE(isScheduleActive(true, true, 23 * 60, 22 * 60, 6 * 60));
    TEST_ASSERT_TRUE(isScheduleActive(true, true, 3 * 60, 22 * 60, 6 * 60));
    TEST_ASSERT_FALSE(isScheduleActive(true, true, 12 * 60, 22 * 60, 6 * 60));
}

void test_schedule_end_is_exclusive() {
    TEST_ASSERT_TRUE(isScheduleActive(true, true, 22 * 60, 22 * 60, 23 * 60));
    TEST_ASSERT_FALSE(isScheduleActive(true, true, 23 * 60, 22 * 60, 23 * 60));
}

void test_dns_matching_accepts_exact_and_subdomains() {
    TEST_ASSERT_TRUE(domainMatches("example.com", "example.com"));
    TEST_ASSERT_TRUE(domainMatches("API.Example.COM", "example.com"));
    TEST_ASSERT_FALSE(domainMatches("badexample.com", "example.com"));
    TEST_ASSERT_FALSE(domainMatches(nullptr, "example.com"));
}

void test_dns_special_domains_are_case_insensitive() {
    TEST_ASSERT_TRUE(isLocalDnsDomain("MICROROUTER.LOCAL"));
    TEST_ASSERT_TRUE(isDoHCanaryDomain("USE-APPLICATION-DNS.NET"));
    TEST_ASSERT_TRUE(isEncryptedDnsEndpoint("dns.google"));
    TEST_ASSERT_TRUE(isEncryptedDnsEndpoint("CLOUDFLARE-DNS.COM"));
    TEST_ASSERT_TRUE(isEncryptedDnsEndpoint("one.one.one.one"));
    TEST_ASSERT_TRUE(isEncryptedDnsEndpoint("dns.quad9.net"));
    TEST_ASSERT_FALSE(isLocalDnsDomain("router.local"));
    TEST_ASSERT_FALSE(isDoHCanaryDomain("example.com"));
    TEST_ASSERT_FALSE(isEncryptedDnsEndpoint("google.com"));
    TEST_ASSERT_FALSE(isEncryptedDnsEndpoint("example.org"));
}

void test_wifi_scan_policy_accepts_only_visible_24ghz_results() {
    TEST_ASSERT_TRUE(isUsableWifiScanResult(1, true));
    TEST_ASSERT_TRUE(isUsableWifiScanResult(14, true));
    TEST_ASSERT_FALSE(isUsableWifiScanResult(0, true));
    TEST_ASSERT_FALSE(isUsableWifiScanResult(36, true));
    TEST_ASSERT_FALSE(isUsableWifiScanResult(6, false));
}

void test_usage_delta_handles_counter_reset() {
    TEST_ASSERT_EQUAL_UINT64(512, byteCounterDelta(1536, 1024));
    TEST_ASSERT_EQUAL_UINT64(256, byteCounterDelta(256, 4096));
    TEST_ASSERT_EQUAL_UINT64(0, byteCounterDelta(4096, 4096));
}

void test_restriction_policy_blocked_device_always_restricted() {
    ClientRestrictionInput input = {};
    input.isBlocked = true;
    input.hasActiveWaiver = true; // Even if waiver exists, manual block takes precedence
    input.isParentalTarget = false;
    TEST_ASSERT_TRUE(evaluateClientRestriction(input));
}

void test_restriction_policy_waiver_bypasses_curfew_and_quotas() {
    ClientRestrictionInput input = {};
    input.isBlocked = false;
    input.hasActiveWaiver = true;
    input.isParentalTarget = true;
    input.isCurfewActive = true;
    input.dailyQuotaEnabled = true;
    input.dailyUsageBytes = 5000;
    input.dailyLimitBytes = 1000;
    TEST_ASSERT_FALSE(evaluateClientRestriction(input));
}

void test_restriction_policy_non_parental_never_curfewed_or_throttled() {
    ClientRestrictionInput input = {};
    input.isBlocked = false;
    input.hasActiveWaiver = false;
    input.isParentalTarget = false;
    input.isCurfewActive = true;
    input.dailyQuotaEnabled = true;
    input.dailyUsageBytes = 999999;
    input.dailyLimitBytes = 1000;
    TEST_ASSERT_FALSE(evaluateClientRestriction(input));
}

void test_restriction_policy_curfew_restricts_parental_device() {
    ClientRestrictionInput input = {};
    input.isBlocked = false;
    input.hasActiveWaiver = false;
    input.isParentalTarget = true;
    input.isCurfewActive = true;
    TEST_ASSERT_TRUE(evaluateClientRestriction(input));
}

void test_restriction_policy_quota_exceeded_restricts_device() {
    ClientRestrictionInput input = {};
    input.isBlocked = false;
    input.hasActiveWaiver = false;
    input.isParentalTarget = true;
    input.isCurfewActive = false;
    input.dailyQuotaEnabled = true;
    input.dailyUsageBytes = 2048;
    input.dailyLimitBytes = 1024;
    TEST_ASSERT_TRUE(evaluateClientRestriction(input));

    input.dailyUsageBytes = 512;
    TEST_ASSERT_FALSE(evaluateClientRestriction(input));
}

void test_rate_limiter_burst_and_replenish() {
    RateLimiter limiter(3, 2); // capacity 3, refill 2 per sec
    uint32_t ip = 0xC0A80402; // 192.168.4.2

    // First 3 requests should be allowed (burst)
    TEST_ASSERT_TRUE(limiter.allow(ip, 1000));
    TEST_ASSERT_TRUE(limiter.allow(ip, 1010));
    TEST_ASSERT_TRUE(limiter.allow(ip, 1020));

    // 4th request within the same 100ms should be throttled
    TEST_ASSERT_FALSE(limiter.allow(ip, 1030));

    // Fast-forward 1 second (should refill 2 tokens)
    TEST_ASSERT_TRUE(limiter.allow(ip, 2030));
    TEST_ASSERT_TRUE(limiter.allow(ip, 2040));
    TEST_ASSERT_FALSE(limiter.allow(ip, 2050));
}

void test_rate_limiter_isolates_ips() {
    RateLimiter limiter(2, 1);
    uint32_t ip1 = 0xC0A80402;
    uint32_t ip2 = 0xC0A80403;

    // Exhaust IP1
    TEST_ASSERT_TRUE(limiter.allow(ip1, 1000));
    TEST_ASSERT_TRUE(limiter.allow(ip1, 1010));
    TEST_ASSERT_FALSE(limiter.allow(ip1, 1020));

    // IP2 must still have full burst tokens
    TEST_ASSERT_TRUE(limiter.allow(ip2, 1020));
    TEST_ASSERT_TRUE(limiter.allow(ip2, 1030));
    TEST_ASSERT_FALSE(limiter.allow(ip2, 1040));
}

void test_restriction_policy_detail_reasons() {
    ClientRestrictionInput input = {};
    input.isBlocked = true;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_ADMIN_BLOCK, evaluateClientRestrictionDetail(input));

    input.isBlocked = false;
    input.hasActiveWaiver = true;
    input.isParentalTarget = true;
    input.isCurfewActive = true;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_NONE, evaluateClientRestrictionDetail(input));

    input.hasActiveWaiver = false;
    input.isParentalTarget = false;
    input.isCurfewActive = true;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_NONE, evaluateClientRestrictionDetail(input));

    input.isParentalTarget = true;
    input.isCurfewActive = true;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_CURFEW, evaluateClientRestrictionDetail(input));

    input.isCurfewActive = false;
    input.hourlyQuotaEnabled = true;
    input.hourlyUsageBytes = 200;
    input.hourlyLimitBytes = 100;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_QUOTA_HOURLY, evaluateClientRestrictionDetail(input));

    input.hourlyUsageBytes = 50;
    input.dailyQuotaEnabled = true;
    input.dailyUsageBytes = 500;
    input.dailyLimitBytes = 250;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_QUOTA_DAILY, evaluateClientRestrictionDetail(input));

    input.dailyUsageBytes = 100;
    TEST_ASSERT_EQUAL_UINT8(RESTRICTION_NONE, evaluateClientRestrictionDetail(input));
}

void test_dns_captive_probe_domains() {
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("captive.apple.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("connectivitycheck.gstatic.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("connectivitycheck.android.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("clients3.google.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("www.msftconnecttest.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("msftconnecttest.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("www.msftncsi.com"));
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("detectportal.firefox.com"));

    TEST_ASSERT_FALSE(isCaptiveProbeDomain("google.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("apple.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("youtube.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("microsoft.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain(nullptr));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_schedule_is_inactive);
    RUN_TEST(test_unsynced_clock_is_inactive);
    RUN_TEST(test_overnight_schedule_covers_both_sides_of_midnight);
    RUN_TEST(test_schedule_end_is_exclusive);
    RUN_TEST(test_dns_matching_accepts_exact_and_subdomains);
    RUN_TEST(test_dns_special_domains_are_case_insensitive);
    RUN_TEST(test_wifi_scan_policy_accepts_only_visible_24ghz_results);
    RUN_TEST(test_usage_delta_handles_counter_reset);
    RUN_TEST(test_restriction_policy_blocked_device_always_restricted);
    RUN_TEST(test_restriction_policy_waiver_bypasses_curfew_and_quotas);
    RUN_TEST(test_restriction_policy_non_parental_never_curfewed_or_throttled);
    RUN_TEST(test_restriction_policy_curfew_restricts_parental_device);
    RUN_TEST(test_restriction_policy_quota_exceeded_restricts_device);
    RUN_TEST(test_restriction_policy_detail_reasons);
    RUN_TEST(test_dns_captive_probe_domains);
    RUN_TEST(test_rate_limiter_burst_and_replenish);
    RUN_TEST(test_rate_limiter_isolates_ips);
    return UNITY_END();
}
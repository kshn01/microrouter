#include <unity.h>

#include "device_fingerprint_policy.h"
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
    TEST_ASSERT_TRUE(isCaptiveProbeDomain("connectivitycheck.samsung.com"));

    TEST_ASSERT_FALSE(isCaptiveProbeDomain("google.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("apple.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("youtube.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain("microsoft.com"));
    TEST_ASSERT_FALSE(isCaptiveProbeDomain(nullptr));
}

void test_waiver_limit_and_pin_policy() {
    TEST_ASSERT_TRUE(canGrantWaiverWithoutPin(0, 2));
    TEST_ASSERT_TRUE(canGrantWaiverWithoutPin(1, 2));
    TEST_ASSERT_FALSE(canGrantWaiverWithoutPin(2, 2));
    TEST_ASSERT_FALSE(canGrantWaiverWithoutPin(3, 2));

    TEST_ASSERT_TRUE(verifyWaiverPin("1234", "1234"));
    TEST_ASSERT_FALSE(verifyWaiverPin("0000", "1234"));
    TEST_ASSERT_FALSE(verifyWaiverPin(nullptr, "1234"));
    TEST_ASSERT_FALSE(verifyWaiverPin("1234", nullptr));
    TEST_ASSERT_FALSE(verifyWaiverPin("1234", ""));
}

void test_is_randomized_mac() {
    // Randomized MAC addresses from real-world devices observed in network
    TEST_ASSERT_TRUE(isRandomizedMac("1a:e0:88:51:57:eb")); // 'a'
    TEST_ASSERT_TRUE(isRandomizedMac("6e:1b:fb:85:26:ca")); // 'e'
    TEST_ASSERT_TRUE(isRandomizedMac("0a:09:3e:eb:72:cf")); // 'a'
    TEST_ASSERT_TRUE(isRandomizedMac("12:1c:1e:50:8d:e0")); // '2'
    TEST_ASSERT_TRUE(isRandomizedMac("86:7a:52:d5:98:33")); // '6'
    TEST_ASSERT_TRUE(isRandomizedMac("7a:11:22:33:44:55")); // 'a'
    TEST_ASSERT_TRUE(isRandomizedMac("FE:AA:BB:CC:DD:EE")); // 'E'

    // Burned-in manufacturer hardware MACs
    TEST_ASSERT_FALSE(isRandomizedMac("24:0a:c4:00:11:22")); // Espressif (4)
    TEST_ASSERT_FALSE(isRandomizedMac("10:b2:32:87:9b:12")); // Hisense (0)
    TEST_ASSERT_FALSE(isRandomizedMac("d4:c1:c8:11:22:33")); // ZTE (4)
    TEST_ASSERT_FALSE(isRandomizedMac("f0:18:98:12:34:56")); // Apple (0)
    TEST_ASSERT_FALSE(isRandomizedMac("b8:27:eb:11:22:33")); // Raspberry Pi (8)
    TEST_ASSERT_FALSE(isRandomizedMac("00:15:5d:11:22:33")); // Microsoft (0)

    // Synthetic local ARP placeholders
    TEST_ASSERT_FALSE(isRandomizedMac("02:00:c0:a8:01:05"));
    TEST_ASSERT_FALSE(isRandomizedMac(nullptr));
    TEST_ASSERT_FALSE(isRandomizedMac(""));
}

void test_generic_hostnames() {
    TEST_ASSERT_TRUE(isGenericHostname(nullptr));
    TEST_ASSERT_TRUE(isGenericHostname(""));
    TEST_ASSERT_TRUE(isGenericHostname("   "));
    TEST_ASSERT_TRUE(isGenericHostname("x"));
    TEST_ASSERT_TRUE(isGenericHostname("unknown"));
    TEST_ASSERT_TRUE(isGenericHostname("UNKNOWN"));
    TEST_ASSERT_TRUE(isGenericHostname("localhost"));
    TEST_ASSERT_TRUE(isGenericHostname("android"));
    TEST_ASSERT_TRUE(isGenericHostname("iPhone"));
    TEST_ASSERT_TRUE(isGenericHostname("IPHONE"));
    TEST_ASSERT_TRUE(isGenericHostname("iPad"));
    TEST_ASSERT_TRUE(isGenericHostname("client"));
    TEST_ASSERT_TRUE(isGenericHostname("device"));
    TEST_ASSERT_TRUE(isGenericHostname("pc"));

    // Specific, device-identifying hostnames
    TEST_ASSERT_FALSE(isGenericHostname("motorola-edge-40-neo"));
    TEST_ASSERT_FALSE(isGenericHostname("MAC-65D18C"));
    TEST_ASSERT_FALSE(isGenericHostname("POCO-M5"));
    TEST_ASSERT_FALSE(isGenericHostname("Kishan's iPhone"));
    TEST_ASSERT_FALSE(isGenericHostname("Galaxy-S21-Ultra"));
    TEST_ASSERT_FALSE(isGenericHostname("DESKTOP-ABC1234"));
}

void test_profile_merge_policy() {
    // Reconnecting device with rotated randomized MAC (same specific hostname)
    TEST_ASSERT_TRUE(canMergeDeviceProfiles("0a:09:3e:eb:72:cf", "motorola-edge-40-neo",
                                           "86:7a:52:d5:98:33", "motorola-edge-40-neo"));
    TEST_ASSERT_TRUE(canMergeDeviceProfiles("6e:1b:fb:85:26:ca", "MAC-65D18C",
                                           "1a:e0:88:51:57:eb", "MAC-65D18C"));

    // Same MAC should not merge
    TEST_ASSERT_FALSE(canMergeDeviceProfiles("1a:e0:88:51:57:eb", "MAC-65D18C",
                                            "1a:e0:88:51:57:eb", "MAC-65D18C"));

    // Generic hostnames must NOT merge two devices
    TEST_ASSERT_FALSE(canMergeDeviceProfiles("0a:09:3e:eb:72:cf", "iPhone",
                                            "86:7a:52:d5:98:33", "iPhone"));
    TEST_ASSERT_FALSE(canMergeDeviceProfiles("0a:09:3e:eb:72:cf", "android",
                                            "86:7a:52:d5:98:33", "android"));

    // Two distinct factory hardware MACs must NOT merge even if hostnames match
    TEST_ASSERT_FALSE(canMergeDeviceProfiles("24:0a:c4:00:11:22", "esp32-node",
                                            "10:b2:32:87:9b:12", "esp32-node"));

    // Different hostnames must NOT merge
    TEST_ASSERT_FALSE(canMergeDeviceProfiles("0a:09:3e:eb:72:cf", "motorola-edge-40-neo",
                                            "86:7a:52:d5:98:33", "POCO-M5"));
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
    RUN_TEST(test_waiver_limit_and_pin_policy);
    RUN_TEST(test_rate_limiter_burst_and_replenish);
    RUN_TEST(test_rate_limiter_isolates_ips);
    RUN_TEST(test_is_randomized_mac);
    RUN_TEST(test_generic_hostnames);
    RUN_TEST(test_profile_merge_policy);
    return UNITY_END();
}
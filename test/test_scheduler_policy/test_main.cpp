#include <unity.h>

#include "dns_policy.h"
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
    TEST_ASSERT_FALSE(isLocalDnsDomain("router.local"));
    TEST_ASSERT_FALSE(isDoHCanaryDomain("example.com"));
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
    return UNITY_END();
}
#include <unity.h>

#include "scheduler_policy.h"

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_schedule_is_inactive);
    RUN_TEST(test_unsynced_clock_is_inactive);
    RUN_TEST(test_overnight_schedule_covers_both_sides_of_midnight);
    RUN_TEST(test_schedule_end_is_exclusive);
    return UNITY_END();
}
#include "unity.h"

#include "core/probe_guard.h"

void setUp(void) {}
void tearDown(void) {}

static void test_good_samples_pass_through(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 3);
    probe_guard_out_t o = probe_guard_update(&g, true, 93.0f, 0);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_FALSE(o.held);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 93.0f, o.celsius);
    TEST_ASSERT_EQUAL_UINT8(0, o.fault);
}

static void test_never_good_probe_fails_immediately(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 3);
    probe_guard_out_t o = probe_guard_update(&g, false, 0.0f, 0x80);
    TEST_ASSERT_FALSE(o.ok);
    TEST_ASSERT_FALSE(o.held);
    TEST_ASSERT_EQUAL_UINT8(0x80, o.fault);
}

static void test_short_bad_run_holds_last_good(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 3);
    probe_guard_update(&g, true, 93.0f, 0);

    probe_guard_out_t o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_TRUE(o.held);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 93.0f, o.celsius);
    TEST_ASSERT_EQUAL_UINT8(0x04, o.fault); /* surfaced for logging */

    o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_TRUE(o.held);

    /* Third consecutive bad sample reaches the trip count. */
    o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_FALSE(o.ok);
    TEST_ASSERT_FALSE(o.held);
    TEST_ASSERT_EQUAL_UINT8(0x04, o.fault);
}

static void test_good_sample_resets_run(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 3);
    probe_guard_update(&g, true, 93.0f, 0);
    probe_guard_update(&g, false, 0.0f, 0x04);
    probe_guard_update(&g, false, 0.0f, 0x04);
    probe_guard_out_t o = probe_guard_update(&g, true, 94.0f, 0);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_FALSE(o.held);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 94.0f, o.celsius);

    /* Run restarts from zero: two more bad samples are still ridden through
     * and hold the newer good value. */
    o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 94.0f, o.celsius);
    o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_TRUE(o.ok);
    o = probe_guard_update(&g, false, 0.0f, 0x04);
    TEST_ASSERT_FALSE(o.ok);
}

static void test_tripped_probe_stays_tripped_and_recovers_on_good(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 2);
    probe_guard_update(&g, true, 93.0f, 0);
    for (int i = 0; i < 300; i++) { /* well past uint8 range: must not wrap */
        probe_guard_out_t o = probe_guard_update(&g, false, 0.0f, 0x80);
        if (i >= 1) {
            TEST_ASSERT_FALSE(o.ok);
        }
    }
    probe_guard_out_t o = probe_guard_update(&g, true, 90.0f, 0);
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, o.celsius);
}

static void test_zero_trip_selects_default(void)
{
    probe_guard_t g;
    probe_guard_init(&g, 0);
    TEST_ASSERT_EQUAL_UINT8(PROBE_GUARD_DEFAULT_TRIP, g.trip);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_good_samples_pass_through);
    RUN_TEST(test_never_good_probe_fails_immediately);
    RUN_TEST(test_short_bad_run_holds_last_good);
    RUN_TEST(test_good_sample_resets_run);
    RUN_TEST(test_tripped_probe_stays_tripped_and_recovers_on_good);
    RUN_TEST(test_zero_trip_selects_default);
    return UNITY_END();
}

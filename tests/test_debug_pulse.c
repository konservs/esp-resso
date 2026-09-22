#include "unity.h"

#include <string.h>

#include "core/debug_pulse.h"

void setUp(void) {}
void tearDown(void) {}

static debug_pulse_t make(void)
{
    debug_pulse_t p;
    debug_pulse_init(&p);
    return p;
}

static void test_starts_idle(void)
{
    debug_pulse_t p = make();
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 0));
    TEST_ASSERT_EQUAL_UINT32(0, debug_pulse_remaining_ms(&p, 0));
}

static void test_drives_then_expires(void)
{
    debug_pulse_t p = make();
    TEST_ASSERT_TRUE(debug_pulse_start(&p, DEBUG_OUT_PUMP, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_PUMP, debug_pulse_active(&p, 1000));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_PUMP, debug_pulse_active(&p, 2999));
    TEST_ASSERT_EQUAL_UINT32(1, debug_pulse_remaining_ms(&p, 2999));
    /* At the deadline it drops the override and stays dropped. */
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 3000));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 3001));
    TEST_ASSERT_EQUAL_UINT32(0, debug_pulse_remaining_ms(&p, 3000));
}

/* The control loop may miss cycles; expiry must depend on the clock, not on
 * being asked at the right moment. */
static void test_expires_even_if_polled_late(void)
{
    debug_pulse_t p = make();
    debug_pulse_start(&p, DEBUG_OUT_FILL_BREW, 500, 2000);
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 60000));
}

static void test_only_one_output_at_a_time(void)
{
    debug_pulse_t p = make();
    debug_pulse_start(&p, DEBUG_OUT_FILL_BREW, 0, 2000);
    debug_pulse_start(&p, DEBUG_OUT_FILL_STEAM, 100, 2000);
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_FILL_STEAM, debug_pulse_active(&p, 200));
    /* The replacement restarts the window from its own start time. */
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_FILL_STEAM, debug_pulse_active(&p, 2099));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 2100));
}

static void test_stop_cancels(void)
{
    debug_pulse_t p = make();
    debug_pulse_start(&p, DEBUG_OUT_PUMP, 0, 2000);
    debug_pulse_stop(&p);
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 10));
}

static void test_rejects_undrivable_output(void)
{
    debug_pulse_t p = make();
    TEST_ASSERT_FALSE(debug_pulse_start(&p, DEBUG_OUT_NONE, 0, 2000));
    TEST_ASSERT_FALSE(debug_pulse_start(&p, DEBUG_OUT_COUNT, 0, 2000));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 0));
}

static void test_duration_is_capped(void)
{
    debug_pulse_t p = make();
    debug_pulse_start(&p, DEBUG_OUT_PUMP, 0, 60000);
    TEST_ASSERT_EQUAL_UINT32(DEBUG_PULSE_MAX_MS, debug_pulse_remaining_ms(&p, 0));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, DEBUG_PULSE_MAX_MS));
}

/* The millisecond clock wraps; a pulse straddling the wrap must still expire. */
static void test_survives_clock_wrap(void)
{
    debug_pulse_t p = make();
    const esp_ms_t start = (esp_ms_t)(0u - 1000u); /* 1 s before wrap */
    debug_pulse_start(&p, DEBUG_OUT_PUMP, start, 2000);
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_PUMP, debug_pulse_active(&p, 500));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_active(&p, 1000));
}

/* Every state that leaves the pump and the fill valves to themselves. */
static void test_permitted_states(void)
{
    static const machine_state_t idle[] = {
        MACHINE_BOOT, MACHINE_HEATING, MACHINE_READY, MACHINE_STEAMING,
        MACHINE_SLEEP
    };
    for (size_t i = 0; i < sizeof(idle) / sizeof(idle[0]); i++) {
        TEST_ASSERT_TRUE(debug_pulse_permitted(idle[i]));
    }
}

static void test_refused_while_pump_is_owned(void)
{
    /* A shot or a backflush owns the pump; an override would fight it. */
    TEST_ASSERT_FALSE(debug_pulse_permitted(MACHINE_BREWING));
    TEST_ASSERT_FALSE(debug_pulse_permitted(MACHINE_BACKFLUSH));
}

/* A safety trip is always about temperature, and no output here is a heater.
 * Blocking on it would make the feature unavailable on exactly the half-wired
 * bench machine it exists to diagnose. */
static void test_allowed_while_faulted(void)
{
    TEST_ASSERT_TRUE(debug_pulse_permitted(MACHINE_FAULT));
}

static void test_names_round_trip(void)
{
    for (int i = 0; i < DEBUG_OUT_COUNT; i++) {
        const char *n = debug_pulse_out_name((debug_out_t)i);
        TEST_ASSERT_EQUAL_INT(i, debug_pulse_out_from_name(n));
    }
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_out_from_name("heater"));
    TEST_ASSERT_EQUAL_INT(DEBUG_OUT_NONE, debug_pulse_out_from_name(NULL));
    TEST_ASSERT_EQUAL_STRING("none", debug_pulse_out_name(DEBUG_OUT_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle);
    RUN_TEST(test_drives_then_expires);
    RUN_TEST(test_expires_even_if_polled_late);
    RUN_TEST(test_only_one_output_at_a_time);
    RUN_TEST(test_stop_cancels);
    RUN_TEST(test_rejects_undrivable_output);
    RUN_TEST(test_duration_is_capped);
    RUN_TEST(test_survives_clock_wrap);
    RUN_TEST(test_permitted_states);
    RUN_TEST(test_refused_while_pump_is_owned);
    RUN_TEST(test_allowed_while_faulted);
    RUN_TEST(test_names_round_trip);
    return UNITY_END();
}

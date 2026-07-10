#include "unity.h"

#include "core/pump_guard.h"

void setUp(void) {}
void tearDown(void) {}

/* Ulka EFX5 rating: 2 min ON / 1 min OFF. */
#define ON_MAX  120000u
#define OFF_MIN  60000u

/* Drive the guard from @p now for @p ms, in 100 ms control-loop steps. */
static esp_ms_t drive(pump_guard_t *g, esp_ms_t now, bool on, uint32_t ms)
{
    for (uint32_t e = 0; e < ms; e += 100) {
        now += 100;
        pump_guard_update(g, on, now);
    }
    return now;
}

static pump_guard_t make_guard(void)
{
    const pump_guard_config_t cfg = { .on_max_ms = ON_MAX, .off_min_ms = OFF_MIN };
    pump_guard_t g;
    pump_guard_init(&g, &cfg);
    pump_guard_update(&g, false, 0); /* seed the timestamp at t=0 */
    return g;
}

/* A guard that has served its startup rest and is ready to brew. Returns the
 * clock so callers can keep driving from there. */
static esp_ms_t make_ready(pump_guard_t *g, esp_ms_t *now)
{
    *g = make_guard();
    *now = drive(g, 0, false, OFF_MIN); /* rest off the startup lock-out */
    return *now;
}

static void test_startup_is_locked_until_rested(void)
{
    /* Fresh power-up: assume the worst and hold off the first shot. */
    pump_guard_t g = make_guard();
    TEST_ASSERT_TRUE(pump_guard_cooling(&g));
    TEST_ASSERT_FALSE(pump_guard_can_brew(&g));
    TEST_ASSERT_UINT32_WITHIN(200, OFF_MIN, pump_guard_cooldown_ms(&g));

    /* An incomplete rest is not enough. */
    esp_ms_t now = drive(&g, 0, false, OFF_MIN - 1000);
    TEST_ASSERT_FALSE(pump_guard_can_brew(&g));

    /* A full rest unlocks the first shot. */
    drive(&g, now, false, 1000);
    TEST_ASSERT_TRUE(pump_guard_can_brew(&g));
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
    TEST_ASSERT_EQUAL_UINT32(0, pump_guard_cooldown_ms(&g));
}

static void test_short_run_never_locks(void)
{
    /* Once rested, a normal ~30 s shot is nowhere near the 2 min budget. */
    pump_guard_t g;
    esp_ms_t now;
    make_ready(&g, &now);
    now = drive(&g, now, true, 30000);
    TEST_ASSERT_TRUE(pump_guard_can_brew(&g));
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
}

static void test_full_on_budget_trips_cooling(void)
{
    pump_guard_t g;
    esp_ms_t now;
    make_ready(&g, &now);

    now = drive(&g, now, true, ON_MAX); /* 2 min continuous */
    TEST_ASSERT_TRUE(pump_guard_cooling(&g));
    TEST_ASSERT_FALSE(pump_guard_can_brew(&g));
    TEST_ASSERT_UINT32_WITHIN(200, OFF_MIN, pump_guard_cooldown_ms(&g));

    /* A partial rest keeps it locked (hysteresis: clears only when drained). */
    now = drive(&g, now, false, OFF_MIN - 1000);
    TEST_ASSERT_TRUE(pump_guard_cooling(&g));

    /* Completing the rest releases the lock. */
    drive(&g, now, false, 1000);
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
    TEST_ASSERT_TRUE(pump_guard_can_brew(&g));
    TEST_ASSERT_EQUAL_UINT32(0, pump_guard_cooldown_ms(&g));
}

static void test_cooldown_counts_down(void)
{
    pump_guard_t g;
    esp_ms_t now;
    make_ready(&g, &now);
    now = drive(&g, now, true, ON_MAX);
    const uint32_t start = pump_guard_cooldown_ms(&g);

    drive(&g, now, false, OFF_MIN / 2); /* rest halfway */
    const uint32_t half = pump_guard_cooldown_ms(&g);

    TEST_ASSERT_TRUE(half < start);
    TEST_ASSERT_UINT32_WITHIN(500, OFF_MIN / 2, half);
}

static void test_rated_duty_recovers_fully(void)
{
    /* At the rated 2:1 ratio the bucket drains as fast as it filled: 60 s ON
     * then 30 s OFF returns to empty without ever tripping. */
    pump_guard_t g;
    esp_ms_t now;
    make_ready(&g, &now);
    now = drive(&g, now, true, 60000);
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
    drive(&g, now, false, 30000);
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
    TEST_ASSERT_EQUAL_UINT32(0, pump_guard_cooldown_ms(&g));
}

static void test_zero_on_max_disables_guard(void)
{
    /* A disabled guard (e.g. a rotary pump) never locks — not even at startup. */
    const pump_guard_config_t cfg = { .on_max_ms = 0, .off_min_ms = OFF_MIN };
    pump_guard_t g;
    pump_guard_init(&g, &cfg);
    pump_guard_update(&g, false, 0);
    TEST_ASSERT_TRUE(pump_guard_can_brew(&g));
    drive(&g, 0, true, 10u * 60000u); /* 10 min flat out */
    TEST_ASSERT_FALSE(pump_guard_cooling(&g));
    TEST_ASSERT_TRUE(pump_guard_can_brew(&g));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_startup_is_locked_until_rested);
    RUN_TEST(test_short_run_never_locks);
    RUN_TEST(test_full_on_budget_trips_cooling);
    RUN_TEST(test_cooldown_counts_down);
    RUN_TEST(test_rated_duty_recovers_fully);
    RUN_TEST(test_zero_on_max_disables_guard);
    return UNITY_END();
}

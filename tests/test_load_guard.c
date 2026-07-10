#include "unity.h"

#include "core/load_guard.h"

void setUp(void) {}
void tearDown(void) {}

static int active_count(const load_guard_output_t *o)
{
    int n = 0;
    for (int i = 0; i < LOAD_HEATER_COUNT; i++) {
        if (o->duty[i] > 0.0f) {
            n++;
        }
    }
    return n;
}

/* Cold start: both boilers want heat, brew still warming. Brew takes both of
 * its elements (priority); steam is capped to its lower element only. */
static void test_warmup_prioritises_brew(void)
{
    const load_guard_input_t in = {
        .brew_duty = 1.0f, .steam_duty = 1.0f, .brew_at_setpoint = false
    };
    const load_guard_output_t o = load_guard_apply(&in, LOAD_DEFAULT_MAX_ACTIVE);

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.duty[LOAD_BREW_LO]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.duty[LOAD_BREW_HI]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.duty[LOAD_STEAM_LO]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.duty[LOAD_STEAM_HI]); /* held off */
    TEST_ASSERT_EQUAL_INT(3, active_count(&o));
}

/* Once brew is up to temperature it holds on its lower element only, freeing a
 * slot so steam can run both of its elements. */
static void test_brew_ready_frees_slot_for_steam(void)
{
    const load_guard_input_t in = {
        .brew_duty = 0.2f, .steam_duty = 0.8f, .brew_at_setpoint = true
    };
    const load_guard_output_t o = load_guard_apply(&in, LOAD_DEFAULT_MAX_ACTIVE);

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, o.duty[LOAD_BREW_LO]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.duty[LOAD_BREW_HI]); /* PID: LO only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, o.duty[LOAD_STEAM_LO]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, o.duty[LOAD_STEAM_HI]);
    TEST_ASSERT_EQUAL_INT(3, active_count(&o));
}

/* A boiler that is not calling for heat uses no elements. */
static void test_brew_idle_gives_steam_both(void)
{
    const load_guard_input_t in = {
        .brew_duty = 0.0f, .steam_duty = 1.0f, .brew_at_setpoint = true
    };
    const load_guard_output_t o = load_guard_apply(&in, LOAD_DEFAULT_MAX_ACTIVE);

    TEST_ASSERT_EQUAL_INT(0, o.duty[LOAD_BREW_LO] > 0.0f);
    TEST_ASSERT_EQUAL_INT(0, o.duty[LOAD_BREW_HI] > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.duty[LOAD_STEAM_LO]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.duty[LOAD_STEAM_HI]);
}

/* Steam alone warms on both elements when brew wants nothing. */
static void test_steam_only(void)
{
    const load_guard_input_t in = {
        .brew_duty = 0.0f, .steam_duty = 1.0f, .brew_at_setpoint = false
    };
    const load_guard_output_t o = load_guard_apply(&in, LOAD_DEFAULT_MAX_ACTIVE);
    TEST_ASSERT_EQUAL_INT(2, active_count(&o));
}

/* Lifting the cap to the element count lets all four run at once. */
static void test_cap_disabled_runs_all_four(void)
{
    const load_guard_input_t in = {
        .brew_duty = 1.0f, .steam_duty = 1.0f, .brew_at_setpoint = false
    };
    const load_guard_output_t o = load_guard_apply(&in, LOAD_HEATER_COUNT);
    TEST_ASSERT_EQUAL_INT(4, active_count(&o));
}

/* The cap is never exceeded, whatever both boilers ask for. */
static void test_never_exceeds_cap(void)
{
    for (uint8_t cap = 3; cap <= LOAD_HEATER_COUNT; cap++) {
        const load_guard_input_t in = {
            .brew_duty = 1.0f, .steam_duty = 1.0f, .brew_at_setpoint = false
        };
        const load_guard_output_t o = load_guard_apply(&in, cap);
        TEST_ASSERT_TRUE(active_count(&o) <= (int)cap);
        /* Brew (priority) always gets its two elements while warming. */
        TEST_ASSERT_TRUE(o.duty[LOAD_BREW_LO] > 0.0f);
        TEST_ASSERT_TRUE(o.duty[LOAD_BREW_HI] > 0.0f);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_warmup_prioritises_brew);
    RUN_TEST(test_brew_ready_frees_slot_for_steam);
    RUN_TEST(test_brew_idle_gives_steam_both);
    RUN_TEST(test_steam_only);
    RUN_TEST(test_cap_disabled_runs_all_four);
    RUN_TEST(test_never_exceeds_cap);
    return UNITY_END();
}

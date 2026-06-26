#include "unity.h"

#include "core/brew.h"

void setUp(void) {}
void tearDown(void) {}

static brew_params_t make_params(void)
{
    brew_params_t p = {
        .preinfuse_ms = 2000,
        .hold_ms = 1000,
        .preinfuse_pump = 0.3f,
        .extract_pump = 1.0f,
        .target_volume_ml = 40.0f,
        .max_shot_ms = 60000
    };
    return p;
}

static void test_auto_profile_stage_sequence(void)
{
    brew_params_t p = make_params();
    brew_profile_t prof;
    brew_profile_build(&prof, BREW_PROFILE_AUTO, &p);
    TEST_ASSERT_EQUAL_UINT(3, prof.stage_count);

    brew_t b;
    brew_init(&b, &prof);
    brew_start(&b, 0);

    /* Pre-infusion: gentle pump. */
    brew_output_t o = brew_update(&b, 0, 0.0f);
    TEST_ASSERT_TRUE(o.pump_on);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.3f, o.pump_duty);

    /* After pre-infusion -> bloom/hold: pump off. */
    o = brew_update(&b, 2500, 0.0f);
    TEST_ASSERT_FALSE(o.pump_on);
    TEST_ASSERT_EQUAL_INT(BREW_PUMP_OFF, o.mode);

    /* After hold -> extraction: full pump. */
    o = brew_update(&b, 3600, 0.0f);
    TEST_ASSERT_TRUE(o.pump_on);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.pump_duty);
    TEST_ASSERT_TRUE(brew_active(&b));
}

static void test_volumetric_stop(void)
{
    brew_params_t p = make_params();
    brew_profile_t prof;
    brew_profile_build(&prof, BREW_PROFILE_AUTO, &p);
    brew_t b;
    brew_init(&b, &prof);
    brew_start(&b, 0);

    (void)brew_update(&b, 100, 0.0f);
    brew_output_t o = brew_update(&b, 4000, 40.0f); /* hit target volume */
    TEST_ASSERT_TRUE(o.done);
    TEST_ASSERT_FALSE(o.pump_on);
    TEST_ASSERT_FALSE(brew_active(&b));
}

static void test_max_time_cap(void)
{
    brew_params_t p = make_params();
    brew_profile_t prof;
    brew_profile_build(&prof, BREW_PROFILE_AUTO, &p);
    brew_t b;
    brew_init(&b, &prof);
    brew_start(&b, 0);

    brew_output_t o = brew_update(&b, 61000, 0.0f); /* exceeds max_shot_ms */
    TEST_ASSERT_TRUE(o.done);
    TEST_ASSERT_FALSE(brew_active(&b));
}

static void test_manual_profile_follows_lever(void)
{
    brew_params_t p = make_params();
    brew_profile_t prof;
    brew_profile_build(&prof, BREW_PROFILE_MANUAL, &p);
    TEST_ASSERT_EQUAL_UINT(1, prof.stage_count);

    brew_t b;
    brew_init(&b, &prof);
    brew_start(&b, 0);

    /* Full pump immediately and stays on regardless of time (no auto stages). */
    brew_output_t o = brew_update(&b, 0, 0.0f);
    TEST_ASSERT_TRUE(o.pump_on);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.pump_duty);
    o = brew_update(&b, 8000, 10.0f);
    TEST_ASSERT_TRUE(o.pump_on);

    /* Releasing the lever stops the shot. */
    brew_stop(&b);
    TEST_ASSERT_FALSE(brew_active(&b));
    o = brew_update(&b, 9000, 10.0f);
    TEST_ASSERT_FALSE(o.pump_on);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_profile_stage_sequence);
    RUN_TEST(test_volumetric_stop);
    RUN_TEST(test_max_time_cap);
    RUN_TEST(test_manual_profile_follows_lever);
    return UNITY_END();
}

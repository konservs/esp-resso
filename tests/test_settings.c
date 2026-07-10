#include "unity.h"

#include "core/settings.h"

void setUp(void) {}
void tearDown(void) {}

static void test_defaults_are_valid(void)
{
    settings_t s;
    settings_load_defaults(&s);
    TEST_ASSERT_EQUAL_INT(ESPRESSO_OK, settings_validate(&s));
}

static void test_brew_setpoint_out_of_range(void)
{
    settings_t s;
    settings_load_defaults(&s);
    s.brew_setpoint = 70.0f; /* below the allowed brew range */
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));
}

static void test_cutoff_must_exceed_setpoints(void)
{
    settings_t s;
    settings_load_defaults(&s);
    s.safety.max_temp = s.steam_setpoint + 1.0f; /* too close to operating temp */
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));
}

static void test_invalid_profile_rejected(void)
{
    settings_t s;
    settings_load_defaults(&s);
    s.active_profile = (brew_profile_type_t)99;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));
}

static void test_pump_duty_cycle_validation(void)
{
    settings_t s;
    settings_load_defaults(&s);

    /* An ON budget with no rest time is inconsistent. */
    s.pump.off_min_ms = 0;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));

    /* A sub-minute pump capacity is rejected (the startup cooldown could then
     * gate a shot for longer than the pump can run). */
    settings_load_defaults(&s);
    s.pump.on_max_ms = 30000;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));

    /* Exactly one minute is the floor and is accepted. */
    s.pump.on_max_ms = 60000;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_OK, settings_validate(&s));

    /* Disabling the guard entirely (no ON budget) is allowed. */
    s.pump.on_max_ms = 0;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_OK, settings_validate(&s));
}

static void test_heater_load_cap_bounds(void)
{
    settings_t s;
    settings_load_defaults(&s);

    /* Fewer than three would starve a boiler; more than four cannot exist. */
    s.max_active_heaters = 2;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));
    s.max_active_heaters = LOAD_HEATER_COUNT + 1;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_ERR_RANGE, settings_validate(&s));

    /* Three (120 V default) and four (cap lifted) are both valid. */
    s.max_active_heaters = 3;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_OK, settings_validate(&s));
    s.max_active_heaters = LOAD_HEATER_COUNT;
    TEST_ASSERT_EQUAL_INT(ESPRESSO_OK, settings_validate(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_brew_setpoint_out_of_range);
    RUN_TEST(test_cutoff_must_exceed_setpoints);
    RUN_TEST(test_invalid_profile_rejected);
    RUN_TEST(test_pump_duty_cycle_validation);
    RUN_TEST(test_heater_load_cap_bounds);
    return UNITY_END();
}

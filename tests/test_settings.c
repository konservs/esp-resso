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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_brew_setpoint_out_of_range);
    RUN_TEST(test_cutoff_must_exceed_setpoints);
    RUN_TEST(test_invalid_profile_rejected);
    return UNITY_END();
}

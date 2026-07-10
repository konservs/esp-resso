/**
 * @file test_hal_mock.c
 * @brief Demonstrates mocking the HAL with fff.
 *
 * The portable core is pure and needs no mocks, but code that talks to the HAL
 * (e.g. task helpers) does. This shows the pattern: fake the HAL functions,
 * exercise the consumer, and assert on the recorded calls. Swap `apply_duty`
 * for a real task helper to unit-test the glue layer off-hardware.
 */
#include "fff.h"
#include "unity.h"

#include "hal/hal_heater.h"

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(hal_heater_set_duty, hal_heater_id_t, float);
FAKE_VOID_FUNC(hal_heater_all_off);

void setUp(void)
{
    RESET_FAKE(hal_heater_set_duty);
    RESET_FAKE(hal_heater_all_off);
    FFF_RESET_HISTORY();
}
void tearDown(void) {}

/* Stand-in for a task helper that drives the heater HAL. */
static void apply_duty(hal_heater_id_t element, float duty)
{
    hal_heater_set_duty(element, duty);
}

static void test_fake_records_arguments(void)
{
    apply_duty(HAL_HEATER_BREW_LO, 0.5f);
    TEST_ASSERT_EQUAL_UINT(1, hal_heater_set_duty_fake.call_count);
    TEST_ASSERT_EQUAL_INT(HAL_HEATER_BREW_LO, hal_heater_set_duty_fake.arg0_val);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, hal_heater_set_duty_fake.arg1_val);
}

static void test_all_off_invoked(void)
{
    hal_heater_all_off();
    TEST_ASSERT_EQUAL_UINT(1, hal_heater_all_off_fake.call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fake_records_arguments);
    RUN_TEST(test_all_off_invoked);
    return UNITY_END();
}

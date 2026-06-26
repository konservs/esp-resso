#include "unity.h"

#include "core/safety.h"

void setUp(void) {}
void tearDown(void) {}

static safety_t make_safety(void)
{
    safety_config_t cfg = { .max_temp = 160.0f, .heat_timeout_ms = 1000 };
    safety_t s;
    safety_init(&s, &cfg);
    return s;
}

static safety_inputs_t healthy_inputs(void)
{
    safety_inputs_t in = {
        .brew_temp = 93.0f,
        .steam_temp = 125.0f,
        .brew_sensor_ok = true,
        .steam_sensor_ok = true,
        .any_heater_on = false,
        .making_progress = true
    };
    return in;
}

static void test_healthy_does_not_trip(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 0));
    TEST_ASSERT_FALSE(safety_tripped(&s));
}

static void test_overtemp_trips_and_latches(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.brew_temp = 170.0f;
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_OVERTEMP, safety_update(&s, &in, 0));
    /* Latches even after the input returns to normal. */
    in = healthy_inputs();
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_OVERTEMP, safety_update(&s, &in, 10));
    TEST_ASSERT_TRUE(safety_tripped(&s));
}

static void test_sensor_fault_trips(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.steam_sensor_ok = false;
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_SENSOR_FAULT, safety_update(&s, &in, 0));
}

static void test_heat_timeout_trips(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.any_heater_on = true;
    in.making_progress = false;

    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 0));    /* timer starts */
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 500));  /* still within */
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_HEAT_TIMEOUT, safety_update(&s, &in, 1000));
}

static void test_progress_resets_heat_timer(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.any_heater_on = true;
    in.making_progress = false;
    safety_update(&s, &in, 0);
    /* Temperature starts progressing -> timer resets, no trip later. */
    in.making_progress = true;
    safety_update(&s, &in, 900);
    in.making_progress = false;
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 1500));
}

static void test_clear_resets(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.brew_temp = 200.0f;
    safety_update(&s, &in, 0);
    TEST_ASSERT_TRUE(safety_tripped(&s));
    safety_clear(&s);
    TEST_ASSERT_FALSE(safety_tripped(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_healthy_does_not_trip);
    RUN_TEST(test_overtemp_trips_and_latches);
    RUN_TEST(test_sensor_fault_trips);
    RUN_TEST(test_heat_timeout_trips);
    RUN_TEST(test_progress_resets_heat_timer);
    RUN_TEST(test_clear_resets);
    return UNITY_END();
}

#include "unity.h"

#include "core/safety.h"

void setUp(void) {}
void tearDown(void) {}

static safety_t make_safety(void)
{
    safety_config_t cfg = { .max_temp = 160.0f,
                            .heat_timeout_ms = 1000,
                            .fill_timeout_ms = 2000 };
    safety_t s;
    safety_init(&s, &cfg);
    return s;
}

static safety_inputs_t healthy_inputs(void)
{
    safety_inputs_t in = {
        .sensors_valid = true,
        .brew_temp = 93.0f,
        .steam_temp = 125.0f,
        .brew_sensor_ok = true,
        .steam_sensor_ok = true,
        .any_heater_on = false,
        .making_progress = true,
        .any_fill_on = false
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

/* Regression: the supervisor ticks before the control loop takes its first
 * sample, so it must not read the zero-initialised inputs as a faulted probe.
 * Doing so latched SENSOR_FAULT on every power-up. */
static void test_no_reading_yet_does_not_trip(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = { 0 }; /* what the shared state looks like at boot */
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 0));
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 50));
    TEST_ASSERT_FALSE(safety_tripped(&s));

    /* ...and once real readings arrive, the rules apply as normal. */
    safety_inputs_t real = healthy_inputs();
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &real, 100));
    real.brew_sensor_ok = false;
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_SENSOR_FAULT, safety_update(&s, &real, 150));
}

/* A heater left on across the boot window must not accumulate run time before
 * there is a temperature to judge progress by. */
static void test_no_reading_yet_holds_heat_timer(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = { 0 };
    in.any_heater_on = true;
    in.making_progress = false;
    safety_update(&s, &in, 0);
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 5000));

    /* The timer starts from the first real reading, not from boot. */
    safety_inputs_t real = healthy_inputs();
    real.any_heater_on = true;
    real.making_progress = false;
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &real, 5000));
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &real, 5900));
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_HEAT_TIMEOUT, safety_update(&s, &real, 6000));
}

/* A boiler that keeps calling for water and never reads full: a leak, a stuck
 * float, a failed pump/valve, or a dead probe. Left alone it pumps at that
 * boiler forever. */
static void test_fill_timeout_trips(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.any_fill_on = true;

    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 0));    /* timer starts */
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 1900)); /* still within */
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_FILL_TIMEOUT, safety_update(&s, &in, 2000));

    /* Latched: it stays tripped once the fill stops. */
    in.any_fill_on = false;
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_FILL_TIMEOUT, safety_update(&s, &in, 2100));
}

/* A fill that completes — the probe reads covered, so the command drops — must
 * not leave the clock running for the next one. */
static void test_completed_fill_resets_the_timer(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = healthy_inputs();
    in.any_fill_on = true;
    safety_update(&s, &in, 0);
    safety_update(&s, &in, 1500);

    in.any_fill_on = false;            /* boiler reached full */
    safety_update(&s, &in, 1600);

    in.any_fill_on = true;             /* a later top-up starts from zero */
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 1700));
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 3000));
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_FILL_TIMEOUT, safety_update(&s, &in, 3700));
}

/* fill_timeout_ms = 0 disables the check (plumbed machine, or bench work). */
static void test_fill_timeout_disabled_when_zero(void)
{
    safety_config_t cfg = { .max_temp = 160.0f,
                            .heat_timeout_ms = 1000,
                            .fill_timeout_ms = 0 };
    safety_t s;
    safety_init(&s, &cfg);
    safety_inputs_t in = healthy_inputs();
    in.any_fill_on = true;
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 0));
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 600000)); /* 10 min */
    TEST_ASSERT_FALSE(safety_tripped(&s));
}

/* A valve left open across the boot window must not accumulate fill time
 * before the control loop has published anything — the same rule the heat
 * timer follows. */
static void test_no_reading_yet_holds_fill_timer(void)
{
    safety_t s = make_safety();
    safety_inputs_t in = { 0 };
    in.any_fill_on = true;
    safety_update(&s, &in, 0);
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &in, 5000));

    safety_inputs_t real = healthy_inputs();
    real.any_fill_on = true;
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &real, 5000));
    TEST_ASSERT_EQUAL_INT(SAFETY_OK, safety_update(&s, &real, 6900));
    TEST_ASSERT_EQUAL_INT(SAFETY_TRIP_FILL_TIMEOUT, safety_update(&s, &real, 7000));
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
    RUN_TEST(test_no_reading_yet_does_not_trip);
    RUN_TEST(test_no_reading_yet_holds_heat_timer);
    RUN_TEST(test_fill_timeout_trips);
    RUN_TEST(test_completed_fill_resets_the_timer);
    RUN_TEST(test_fill_timeout_disabled_when_zero);
    RUN_TEST(test_no_reading_yet_holds_fill_timer);
    RUN_TEST(test_clear_resets);
    return UNITY_END();
}

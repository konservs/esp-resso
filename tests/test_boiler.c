#include "unity.h"

#include "core/boiler.h"

void setUp(void) {}
void tearDown(void) {}

static pid_config_t duty_pid(void)
{
    pid_config_t c = {
        .kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
        .out_min = 0.0f, .out_max = 1.0f,
        .integ_min = 0.0f, .integ_max = 1.0f
    };
    return c;
}

static void test_ready_band(void)
{
    boiler_t b;
    pid_config_t cfg = duty_pid();
    boiler_init(&b, 93.0f, &cfg, 1.0f);
    TEST_ASSERT_TRUE(boiler_at_setpoint(&b, 93.5f));
    TEST_ASSERT_TRUE(boiler_at_setpoint(&b, 92.2f));
    TEST_ASSERT_FALSE(boiler_at_setpoint(&b, 95.0f));
}

static void test_heats_when_cold(void)
{
    boiler_t b;
    pid_config_t cfg = duty_pid();
    boiler_init(&b, 93.0f, &cfg, 1.0f);
    /* 80 C, error 13, kp*err = 1.3 -> clamped to full duty. */
    boiler_output_t o = boiler_update(&b, 80.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, o.heater_duty);
    TEST_ASSERT_FALSE(o.at_setpoint);
}

static void test_off_when_over(void)
{
    boiler_t b;
    pid_config_t cfg = duty_pid();
    boiler_init(&b, 93.0f, &cfg, 1.0f);
    /* Above setpoint: negative term clamps to 0 duty. */
    boiler_output_t o = boiler_update(&b, 98.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.heater_duty);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ready_band);
    RUN_TEST(test_heats_when_cold);
    RUN_TEST(test_off_when_over);
    return UNITY_END();
}

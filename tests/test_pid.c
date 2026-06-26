#include "unity.h"

#include "core/pid.h"

void setUp(void) {}
void tearDown(void) {}

static pid_config_t make_cfg(float kp, float ki, float kd)
{
    pid_config_t c = {
        .kp = kp, .ki = ki, .kd = kd,
        .out_min = -100.0f, .out_max = 100.0f,
        .integ_min = -100.0f, .integ_max = 100.0f
    };
    return c;
}

static void test_proportional_only(void)
{
    pid_config_t cfg = make_cfg(2.0f, 0.0f, 0.0f);
    pid_ctrl_t pid;
    pid_init(&pid, &cfg);
    /* error = 30 - 20 = 10, output = kp*error = 20. */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, pid_update(&pid, 30.0f, 20.0f, 1.0f));
}

static void test_output_is_clamped(void)
{
    pid_config_t cfg = make_cfg(2.0f, 0.0f, 0.0f);
    pid_ctrl_t pid;
    pid_init(&pid, &cfg);
    /* Huge error would give 2000; must clamp to out_max. */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, pid_update(&pid, 1000.0f, 0.0f, 1.0f));
}

static void test_integrator_anti_windup(void)
{
    pid_config_t cfg = {
        .kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
        .out_min = 0.0f, .out_max = 10.0f,
        .integ_min = 0.0f, .integ_max = 1.0f
    };
    pid_ctrl_t pid;
    pid_init(&pid, &cfg);
    /* Constant +1 error each second; integrator must saturate at integ_max=1. */
    float out = 0.0f;
    for (int i = 0; i < 50; i++) {
        out = pid_update(&pid, 1.0f, 0.0f, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, out);
}

static void test_derivative_on_measurement(void)
{
    pid_config_t cfg = make_cfg(0.0f, 0.0f, 1.0f);
    pid_ctrl_t pid;
    pid_init(&pid, &cfg);
    /* First call seeds derivative history -> no kick. */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, pid_update(&pid, 50.0f, 20.0f, 1.0f));
    /* Measurement rises by 2 over 1 s -> d = -kd * 2 = -2. */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.0f, pid_update(&pid, 50.0f, 22.0f, 1.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_proportional_only);
    RUN_TEST(test_output_is_clamped);
    RUN_TEST(test_integrator_anti_windup);
    RUN_TEST(test_derivative_on_measurement);
    return UNITY_END();
}

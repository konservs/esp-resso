#include "unity.h"

#include "core/rtd.h"

void setUp(void) {}
void tearDown(void) {}

/* IEC 60751 PT100 reference points. */
static void test_pt100_zero_celsius(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, rtd_resistance_to_celsius(100.0f, RTD_PT100_R0));
}

static void test_pt100_fifty_celsius(void)
{
    /* PT100 reads ~119.40 ohm at 50 C. */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, rtd_resistance_to_celsius(119.40f, RTD_PT100_R0));
}

static void test_pt100_hundred_celsius(void)
{
    /* PT100 reads ~138.51 ohm at 100 C. */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, rtd_resistance_to_celsius(138.51f, RTD_PT100_R0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pt100_zero_celsius);
    RUN_TEST(test_pt100_fifty_celsius);
    RUN_TEST(test_pt100_hundred_celsius);
    return UNITY_END();
}

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

static void test_zero_resistance_is_implausible(void)
{
    /* A dead/absent front-end returns ~0 ohm, which the CVD math would turn into
     * a plausible-looking ~-247 C. It must be rejected as a fault instead. */
    TEST_ASSERT_FALSE(rtd_resistance_plausible(0.0f, RTD_PT100_R0));
    TEST_ASSERT_FALSE(rtd_resistance_plausible(20.0f, RTD_PT100_R0)); /* ~-195 C */
    /* Sanity: the value we're guarding against really is ~-247 C. */
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -246.9f,
                             rtd_resistance_to_celsius(0.0f, RTD_PT100_R0));
}

static void test_operating_range_is_plausible(void)
{
    TEST_ASSERT_TRUE(rtd_resistance_plausible(100.0f, RTD_PT100_R0));  /* 0 C     */
    TEST_ASSERT_TRUE(rtd_resistance_plausible(147.8f, RTD_PT100_R0));  /* ~125 C  */
    TEST_ASSERT_TRUE(rtd_resistance_plausible(162.9f, RTD_PT100_R0));  /* ~165 C  */
    TEST_ASSERT_TRUE(rtd_resistance_plausible(84.0f, RTD_PT100_R0));   /* ~-40 C  */
}

static void test_absurdly_high_resistance_is_implausible(void)
{
    /* Well past any boiler temperature — an open/short reading high. */
    TEST_ASSERT_FALSE(rtd_resistance_plausible(300.0f, RTD_PT100_R0));
    /* A degenerate R0 is never plausible. */
    TEST_ASSERT_FALSE(rtd_resistance_plausible(100.0f, 0.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pt100_zero_celsius);
    RUN_TEST(test_pt100_fifty_celsius);
    RUN_TEST(test_pt100_hundred_celsius);
    RUN_TEST(test_zero_resistance_is_implausible);
    RUN_TEST(test_operating_range_is_plausible);
    RUN_TEST(test_absurdly_high_resistance_is_implausible);
    return UNITY_END();
}

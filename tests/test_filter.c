#include "unity.h"

#include "core/filter.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ema_seeds_then_smooths(void)
{
    ema_t f;
    ema_init(&f, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, ema_update(&f, 10.0f)); /* seed */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 15.0f, ema_update(&f, 20.0f)); /* halfway */
}

static void test_debounce_requires_consecutive_samples(void)
{
    debounce_t d;
    debounce_init(&d, 3, false);
    TEST_ASSERT_FALSE(debounce_update(&d, true)); /* 1 */
    TEST_ASSERT_FALSE(debounce_update(&d, true)); /* 2 */
    TEST_ASSERT_TRUE(debounce_update(&d, true));  /* 3 -> accept */
}

static void test_debounce_resets_on_disagreement(void)
{
    debounce_t d;
    debounce_init(&d, 3, false);
    debounce_update(&d, true);
    debounce_update(&d, true);
    debounce_update(&d, false); /* breaks the streak */
    TEST_ASSERT_FALSE(debounce_update(&d, true));
    TEST_ASSERT_FALSE(debounce_update(&d, true));
    TEST_ASSERT_TRUE(debounce_update(&d, true));
}

static void test_hysteresis_band(void)
{
    hysteresis_t h;
    hysteresis_init(&h, 10.0f, 20.0f, false);
    TEST_ASSERT_TRUE(hysteresis_update(&h, 5.0f));   /* below low -> on  */
    TEST_ASSERT_TRUE(hysteresis_update(&h, 15.0f));  /* between -> hold  */
    TEST_ASSERT_FALSE(hysteresis_update(&h, 25.0f)); /* above high -> off */
    TEST_ASSERT_FALSE(hysteresis_update(&h, 15.0f)); /* between -> hold  */
    TEST_ASSERT_TRUE(hysteresis_update(&h, 8.0f));   /* below low -> on  */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ema_seeds_then_smooths);
    RUN_TEST(test_debounce_requires_consecutive_samples);
    RUN_TEST(test_debounce_resets_on_disagreement);
    RUN_TEST(test_hysteresis_band);
    return UNITY_END();
}

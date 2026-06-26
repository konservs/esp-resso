#include "unity.h"

#include "core/buttons.h"

void setUp(void) {}
void tearDown(void) {}

static buttons_t make_buttons(void)
{
    button_config_t cfg = { .hold_ms = 500, .repeat_ms = 100 };
    buttons_t b;
    buttons_init(&b, &cfg);
    return b;
}

static void test_short_press_is_a_tap(void)
{
    buttons_t b = make_buttons();
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttons_update(&b, true, false, 0));
    TEST_ASSERT_EQUAL_INT(BTN_A_TAP, buttons_update(&b, false, false, 100));
}

static void test_hold_fires_then_repeats(void)
{
    buttons_t b = make_buttons();
    buttons_update(&b, true, false, 0);
    TEST_ASSERT_EQUAL_INT(BTN_A_HOLD, buttons_update(&b, true, false, 600)); /* threshold */
    TEST_ASSERT_EQUAL_INT(BTN_NONE,   buttons_update(&b, true, false, 650)); /* < repeat  */
    TEST_ASSERT_EQUAL_INT(BTN_A_HOLD, buttons_update(&b, true, false, 720)); /* repeat    */
}

static void test_both_short_is_both_tap(void)
{
    buttons_t b = make_buttons();
    buttons_update(&b, true, true, 0);
    TEST_ASSERT_EQUAL_INT(BTN_BOTH_TAP, buttons_update(&b, false, false, 100));
}

static void test_both_held_enters_config(void)
{
    buttons_t b = make_buttons();
    buttons_update(&b, true, true, 0);
    TEST_ASSERT_EQUAL_INT(BTN_BOTH_HOLD, buttons_update(&b, true, true, 600));
}

static void test_chord_suppresses_single_tap(void)
{
    buttons_t b = make_buttons();
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttons_update(&b, true, false, 0));  /* A down  */
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttons_update(&b, true, true, 50));  /* B joins */
    TEST_ASSERT_EQUAL_INT(BTN_BOTH_TAP, buttons_update(&b, false, false, 150));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_a_tap);
    RUN_TEST(test_hold_fires_then_repeats);
    RUN_TEST(test_both_short_is_both_tap);
    RUN_TEST(test_both_held_enters_config);
    RUN_TEST(test_chord_suppresses_single_tap);
    return UNITY_END();
}

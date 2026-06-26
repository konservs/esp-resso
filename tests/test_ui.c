#include "unity.h"

#include "core/settings.h"
#include "core/ui.h"

void setUp(void) {}
void tearDown(void) {}

static void test_enter_config_edit_and_persist_flag(void)
{
    settings_t s;
    settings_load_defaults(&s);
    const float orig = s.brew_setpoint;

    ui_t ui;
    ui_init(&ui, &s);
    TEST_ASSERT_EQUAL_INT(UI_STATUS, ui_screen(&ui));

    ui_handle(&ui, BTN_BOTH_HOLD); /* STATUS -> MENU */
    TEST_ASSERT_EQUAL_INT(UI_MENU, ui_screen(&ui));
    TEST_ASSERT_EQUAL_INT(UI_ITEM_PROFILE, ui_item(&ui));

    ui_handle(&ui, BTN_B_TAP); /* scroll to Brew temp */
    TEST_ASSERT_EQUAL_INT(UI_ITEM_BREW_TEMP, ui_item(&ui));

    ui_handle(&ui, BTN_BOTH_TAP); /* enter EDIT */
    TEST_ASSERT_EQUAL_INT(UI_EDIT, ui_screen(&ui));

    ui_handle(&ui, BTN_B_TAP); /* +0.5 C */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, orig + 0.5f, s.brew_setpoint);
    TEST_ASSERT_TRUE(ui_take_dirty(&ui));
    TEST_ASSERT_FALSE(ui_take_dirty(&ui)); /* flag cleared */

    ui_handle(&ui, BTN_BOTH_TAP); /* back to MENU */
    TEST_ASSERT_EQUAL_INT(UI_MENU, ui_screen(&ui));
}

static void test_profile_selection_cycles(void)
{
    settings_t s;
    settings_load_defaults(&s);
    s.active_profile = BREW_PROFILE_AUTO;

    ui_t ui;
    ui_init(&ui, &s);
    ui_handle(&ui, BTN_BOTH_HOLD); /* MENU at Profile */
    ui_handle(&ui, BTN_BOTH_TAP);  /* EDIT */
    ui_handle(&ui, BTN_B_TAP);     /* AUTO -> wraps to MANUAL */
    TEST_ASSERT_EQUAL_INT(BREW_PROFILE_MANUAL, s.active_profile);
}

static void test_exit_item_returns_to_status(void)
{
    settings_t s;
    settings_load_defaults(&s);
    ui_t ui;
    ui_init(&ui, &s);
    ui_handle(&ui, BTN_BOTH_HOLD); /* MENU at Profile (item 0) */
    ui_handle(&ui, BTN_A_TAP);     /* scroll up -> wraps to Exit */
    TEST_ASSERT_EQUAL_INT(UI_ITEM_EXIT, ui_item(&ui));
    ui_handle(&ui, BTN_BOTH_TAP);  /* select Exit -> STATUS */
    TEST_ASSERT_EQUAL_INT(UI_STATUS, ui_screen(&ui));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enter_config_edit_and_persist_flag);
    RUN_TEST(test_profile_selection_cycles);
    RUN_TEST(test_exit_item_returns_to_status);
    return UNITY_END();
}

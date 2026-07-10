#include "unity.h"

#include "core/state_machine.h"

void setUp(void) {}
void tearDown(void) {}

static machine_t ready_machine(void)
{
    machine_t m;
    machine_init(&m);
    machine_dispatch(&m, EV_WAKE);                /* BOOT -> HEATING */
    machine_dispatch(&m, EV_BOTH_BOILERS_READY);  /* HEATING -> READY */
    return m;
}

static void test_boot_to_ready(void)
{
    machine_t m;
    machine_init(&m);
    TEST_ASSERT_EQUAL_INT(MACHINE_BOOT, machine_state(&m));
    TEST_ASSERT_EQUAL_INT(MACHINE_HEATING, machine_dispatch(&m, EV_WAKE));
    TEST_ASSERT_EQUAL_INT(MACHINE_READY, machine_dispatch(&m, EV_BOTH_BOILERS_READY));
}

static void test_brew_requires_ready(void)
{
    machine_t m;
    machine_init(&m);
    machine_dispatch(&m, EV_WAKE); /* HEATING */
    /* Lever while still heating must NOT start a shot. */
    TEST_ASSERT_EQUAL_INT(MACHINE_HEATING, machine_dispatch(&m, EV_BREW_LEVER_ON));
}

static void test_brew_cycle(void)
{
    machine_t m = ready_machine();
    TEST_ASSERT_EQUAL_INT(MACHINE_BREWING, machine_dispatch(&m, EV_BREW_LEVER_ON));
    TEST_ASSERT_EQUAL_INT(MACHINE_READY, machine_dispatch(&m, EV_BREW_LEVER_OFF));
}

static void test_steam_cycle(void)
{
    machine_t m = ready_machine();
    TEST_ASSERT_EQUAL_INT(MACHINE_STEAMING, machine_dispatch(&m, EV_STEAM_ON));
    TEST_ASSERT_EQUAL_INT(MACHINE_READY, machine_dispatch(&m, EV_STEAM_OFF));
}

static void test_fault_latches_until_cleared(void)
{
    machine_t m = ready_machine();
    TEST_ASSERT_EQUAL_INT(MACHINE_FAULT, machine_dispatch(&m, EV_FAULT));
    /* Other events are ignored while faulted. */
    TEST_ASSERT_EQUAL_INT(MACHINE_FAULT, machine_dispatch(&m, EV_BREW_LEVER_ON));
    TEST_ASSERT_EQUAL_INT(MACHINE_FAULT, machine_dispatch(&m, EV_BOTH_BOILERS_READY));
    /* Only an explicit clear leaves FAULT. */
    TEST_ASSERT_EQUAL_INT(MACHINE_BOOT, machine_dispatch(&m, EV_FAULT_CLEAR));
}

static void test_no_sleep_while_brewing(void)
{
    machine_t m = ready_machine();
    machine_dispatch(&m, EV_BREW_LEVER_ON); /* BREWING */
    TEST_ASSERT_EQUAL_INT(MACHINE_BREWING, machine_dispatch(&m, EV_SLEEP));
}

static void test_pump_cooling_blocks_brew(void)
{
    machine_t m = ready_machine();
    machine_set_pump_ready(&m, false); /* pump resting on its duty cycle */

    /* The lever is ignored while the pump must rest: stay in READY. */
    TEST_ASSERT_EQUAL_INT(MACHINE_READY, machine_dispatch(&m, EV_BREW_LEVER_ON));
    /* Backflush (also pump-driven) is likewise held off. */
    TEST_ASSERT_EQUAL_INT(MACHINE_READY, machine_dispatch(&m, EV_BACKFLUSH));
    /* Steam does not use the pump, so it is still allowed. */
    TEST_ASSERT_EQUAL_INT(MACHINE_STEAMING, machine_dispatch(&m, EV_STEAM_ON));
    machine_dispatch(&m, EV_STEAM_OFF);

    /* Once the pump has recovered, the lever starts a shot again. */
    machine_set_pump_ready(&m, true);
    TEST_ASSERT_EQUAL_INT(MACHINE_BREWING, machine_dispatch(&m, EV_BREW_LEVER_ON));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_to_ready);
    RUN_TEST(test_brew_requires_ready);
    RUN_TEST(test_brew_cycle);
    RUN_TEST(test_steam_cycle);
    RUN_TEST(test_fault_latches_until_cleared);
    RUN_TEST(test_no_sleep_while_brewing);
    RUN_TEST(test_pump_cooling_blocks_brew);
    return UNITY_END();
}

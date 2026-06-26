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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_to_ready);
    RUN_TEST(test_brew_requires_ready);
    RUN_TEST(test_brew_cycle);
    RUN_TEST(test_steam_cycle);
    RUN_TEST(test_fault_latches_until_cleared);
    RUN_TEST(test_no_sleep_while_brewing);
    return UNITY_END();
}

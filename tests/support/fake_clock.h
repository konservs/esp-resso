/**
 * @file fake_clock.h
 * @brief Deterministic millisecond clock for tests.
 *
 * Core modules take `now` as a parameter, so tests just drive this clock
 * forward to exercise time-dependent behaviour (PID dt, brew stage timing,
 * button holds, safety timeouts) reproducibly.
 */
#ifndef ESPRESSO_TEST_FAKE_CLOCK_H
#define ESPRESSO_TEST_FAKE_CLOCK_H

#include "core/types.h"

typedef struct {
    esp_ms_t now;
} fake_clock_t;

void     fake_clock_init(fake_clock_t *c);
esp_ms_t fake_clock_now(const fake_clock_t *c);
void     fake_clock_advance(fake_clock_t *c, uint32_t ms);

#endif /* ESPRESSO_TEST_FAKE_CLOCK_H */

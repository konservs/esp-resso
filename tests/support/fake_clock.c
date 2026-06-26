#include "fake_clock.h"

void fake_clock_init(fake_clock_t *c)
{
    c->now = 0;
}

esp_ms_t fake_clock_now(const fake_clock_t *c)
{
    return c->now;
}

void fake_clock_advance(fake_clock_t *c, uint32_t ms)
{
    c->now += ms;
}

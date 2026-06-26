#include "core/filter.h"

/* -------------------------------------------------------------------------- */
/* EMA                                                                        */
/* -------------------------------------------------------------------------- */

void ema_init(ema_t *f, float alpha)
{
    f->alpha = espresso_clampf(alpha, 0.0f, 1.0f);
    f->value = 0.0f;
    f->init = false;
}

void ema_reset(ema_t *f)
{
    f->value = 0.0f;
    f->init = false;
}

float ema_update(ema_t *f, float x)
{
    if (!f->init) {
        f->value = x;
        f->init = true;
    } else {
        f->value += f->alpha * (x - f->value);
    }
    return f->value;
}

/* -------------------------------------------------------------------------- */
/* Debounce                                                                   */
/* -------------------------------------------------------------------------- */

void debounce_init(debounce_t *d, uint8_t threshold, bool initial)
{
    d->state = initial;
    d->count = 0;
    d->threshold = threshold == 0 ? 1 : threshold;
}

bool debounce_update(debounce_t *d, bool raw)
{
    if (raw == d->state) {
        d->count = 0;
        return d->state;
    }
    if (++d->count >= d->threshold) {
        d->state = raw;
        d->count = 0;
    }
    return d->state;
}

/* -------------------------------------------------------------------------- */
/* Hysteresis                                                                 */
/* -------------------------------------------------------------------------- */

void hysteresis_init(hysteresis_t *h, float low, float high, bool initial)
{
    h->low = low;
    h->high = high;
    h->active = initial;
}

bool hysteresis_update(hysteresis_t *h, float x)
{
    if (x <= h->low) {
        h->active = true;
    } else if (x >= h->high) {
        h->active = false;
    }
    /* Between the thresholds the previous state is retained. */
    return h->active;
}

#include "core/state_machine.h"

void machine_init(machine_t *m)
{
    m->state = MACHINE_BOOT;
    m->pump_ready = true;
}

void machine_set_pump_ready(machine_t *m, bool ready)
{
    m->pump_ready = ready;
}

machine_state_t machine_state(const machine_t *m)
{
    return m->state;
}

const char *machine_state_name(machine_state_t s)
{
    switch (s) {
    case MACHINE_BOOT:      return "BOOT";
    case MACHINE_HEATING:   return "HEATING";
    case MACHINE_READY:     return "READY";
    case MACHINE_BREWING:   return "BREWING";
    case MACHINE_STEAMING:  return "STEAMING";
    case MACHINE_BACKFLUSH: return "BACKFLUSH";
    case MACHINE_SLEEP:     return "SLEEP";
    case MACHINE_FAULT:     return "FAULT";
    default:                return "?";
    }
}

machine_state_t machine_dispatch(machine_t *m, machine_event_t ev)
{
    /* A fault trips from anywhere and latches until explicitly cleared. */
    if (ev == EV_FAULT) {
        m->state = MACHINE_FAULT;
        return m->state;
    }
    if (m->state == MACHINE_FAULT) {
        if (ev == EV_FAULT_CLEAR) {
            m->state = MACHINE_BOOT;
        }
        return m->state;
    }

    /* Sleep is reachable from any non-fault, non-brewing state. */
    if (ev == EV_SLEEP && m->state != MACHINE_BREWING) {
        m->state = MACHINE_SLEEP;
        return m->state;
    }

    switch (m->state) {
    case MACHINE_BOOT:
        if (ev == EV_WAKE) {
            m->state = MACHINE_HEATING;
        }
        break;

    case MACHINE_SLEEP:
        if (ev == EV_WAKE) {
            m->state = MACHINE_HEATING;
        }
        break;

    case MACHINE_HEATING:
        if (ev == EV_BOTH_BOILERS_READY) {
            m->state = MACHINE_READY;
        }
        break;

    case MACHINE_READY:
        /* Pump-driven cycles are held off while the pump is cooling down; the
         * lever/backflush request is simply ignored (the UI shows why). */
        if (ev == EV_BREW_LEVER_ON) {
            if (m->pump_ready) {
                m->state = MACHINE_BREWING;
            }
        } else if (ev == EV_STEAM_ON) {
            m->state = MACHINE_STEAMING;
        } else if (ev == EV_BACKFLUSH) {
            if (m->pump_ready) {
                m->state = MACHINE_BACKFLUSH;
            }
        } else if (ev == EV_NOT_READY) {
            m->state = MACHINE_HEATING;
        }
        break;

    case MACHINE_BREWING:
        if (ev == EV_BREW_LEVER_OFF) {
            m->state = MACHINE_READY;
        }
        break;

    case MACHINE_STEAMING:
        if (ev == EV_STEAM_OFF) {
            m->state = MACHINE_READY;
        }
        break;

    case MACHINE_BACKFLUSH:
        if (ev == EV_BREW_LEVER_OFF) {
            m->state = MACHINE_READY;
        }
        break;

    default:
        break;
    }

    return m->state;
}

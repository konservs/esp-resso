/**
 * @file hal_esp32_sensors.c
 * @brief Flow meter (pulse counter) and water-level probes.
 */
#include "hal/hal_flow.h"
#include "hal/hal_level.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_rom_sys.h"

#include "drivers/pins.h"

/* Flow meter K-factor. TODO: calibrate against a measured volume for your
 * specific meter (e.g. weigh the output and divide pulses by grams). */
#define FLOW_PULSES_PER_ML 5.0f

static pcnt_unit_handle_t s_flow_unit;

espresso_result_t hal_flow_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 32767,
        .low_limit = -1,
    };
    if (pcnt_new_unit(&unit_cfg, &s_flow_unit) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = PIN_FLOW_PULSE,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t chan = NULL;
    if (pcnt_new_channel(s_flow_unit, &chan_cfg, &chan) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_HOLD);

    if (pcnt_unit_enable(s_flow_unit) != ESP_OK ||
        pcnt_unit_clear_count(s_flow_unit) != ESP_OK ||
        pcnt_unit_start(s_flow_unit) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    return ESPRESSO_OK;
}

uint32_t hal_flow_pulses(void)
{
    int count = 0;
    pcnt_unit_get_count(s_flow_unit, &count);
    return (uint32_t)(count < 0 ? 0 : count);
}

float hal_flow_ml(void)
{
    return (float)hal_flow_pulses() / FLOW_PULSES_PER_ML;
}

void hal_flow_reset(void)
{
    pcnt_unit_clear_count(s_flow_unit);
}

/* -------------------------------------------------------------------------- */
/* Water level - isolated bipolar (±12 V) conductivity sensing.                */
/* See docs/level-sensing.md.                                                  */
/*                                                                            */
/* SELECT/ENABLE/REVERSE feed a 74HC139 decoder (MCU side); its four outputs    */
/* each light an opto that switches a rod to ±12 V - so only ONE rod is ever    */
/* energised and P/N can never conflict (hardware interlock). The selected rod  */
/* is pushed to +12 V then -12 V vs. the earthed body (zero net DC); a per-     */
/* boiler, per-direction opto reports conduction, and a 74HC157 (SELECT) routes */
/* the active boiler's POS/NEG optos onto two MCU inputs. Real water conducts   */
/* on BOTH directions in step with the drive, so we require both - and probe at */
/* two frequencies to reject fixed-frequency pump/heater/mains noise. Sensing   */
/* is pulsed (drive idled between reads).                                       */
/* -------------------------------------------------------------------------- */

/* 74HC139 enable is active-low: LOW enables the decoder (exactly one switch on),
 * HIGH forces all switches off. A pull-up on this line keeps the drive disabled
 * at boot, before hal_level_init() runs. */
#define LEVEL_DRIVE_ENABLE  0
#define LEVEL_DRIVE_IDLE    1

/* SELECT value per boiler (also selects the 74HC157 sense mux channel). */
#define LEVEL_SELECT_BREW   0
#define LEVEL_SELECT_STEAM  1

/* REVERSE value per polarity. 0 -> rod +12 V (read SENSE_POS); 1 -> -12 V (NEG). */
#define LEVEL_POS           0
#define LEVEL_NEG           1

#define LEVEL_CYCLES        4    /* +/- pairs per burst                        */
#define LEVEL_WET_MIN_HITS  3    /* of LEVEL_CYCLES, per direction => that dir wet */
#define LEVEL_DWELL_FAST_US 250  /* ~1.7 kHz probe (also = opto/settle time)   */
#define LEVEL_DWELL_SLOW_US 600  /* ~0.8 kHz probe (second frequency)          */
#define LEVEL_GAP_US        50   /* idle gap between polarity flips            */

/* Sense opto output is active-low (its transistor pulls the pulled-up MCU input
 * to GND while the water conducts). Invert here if your wiring differs. */
static inline bool level_conducting(int gpio)
{
    return gpio_get_level(gpio) == 0;
}

static void level_idle(void)
{
    gpio_set_level(PIN_LEVEL_ENABLE, LEVEL_DRIVE_IDLE);
}

/* Energise the selected rod at one polarity. Idle first so SELECT/REVERSE settle
 * with all switches off (break-before-make); the decoder then enables exactly
 * one switch. */
static void level_drive(int select, int reverse)
{
    gpio_set_level(PIN_LEVEL_ENABLE, LEVEL_DRIVE_IDLE);
    gpio_set_level(PIN_LEVEL_SELECT, select);
    gpio_set_level(PIN_LEVEL_REVERSE, reverse);
    gpio_set_level(PIN_LEVEL_ENABLE, LEVEL_DRIVE_ENABLE);
}

espresso_result_t hal_level_init(void)
{
    /* Control outputs: SELECT / ENABLE / REVERSE. Start idle (drive disabled). */
    gpio_config_t drv = {
        .pin_bit_mask = (1ULL << PIN_LEVEL_SELECT) | (1ULL << PIN_LEVEL_ENABLE) |
                        (1ULL << PIN_LEVEL_REVERSE),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&drv);
    gpio_set_level(PIN_LEVEL_ENABLE, LEVEL_DRIVE_IDLE);
    gpio_set_level(PIN_LEVEL_SELECT, LEVEL_SELECT_BREW);
    gpio_set_level(PIN_LEVEL_REVERSE, LEVEL_POS);

    /* Sense + reservoir inputs. GPIO35/36/39 are input-only with no internal
     * pulls; external pull-ups on the sense-mux outputs / float switch pull the
     * line to GND when active. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_LEVEL_SENSE_POS) |
                        (1ULL << PIN_LEVEL_SENSE_NEG) |
                        (1ULL << PIN_LEVEL_RESERVOIR),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);
    return ESPRESSO_OK;
}

/* One burst at a given dwell (excitation frequency): alternate +/-, counting the
 * conducting half-cycles per direction. Leaves the drive idle. */
static void level_burst(int select, int dwell_us, int *pos_hits, int *neg_hits)
{
    int ph = 0, nh = 0;
    for (int i = 0; i < LEVEL_CYCLES; i++) {
        level_drive(select, LEVEL_POS);
        esp_rom_delay_us(dwell_us);
        if (level_conducting(PIN_LEVEL_SENSE_POS)) {
            ph++;
        }
        level_idle();
        esp_rom_delay_us(LEVEL_GAP_US);

        level_drive(select, LEVEL_NEG);
        esp_rom_delay_us(dwell_us);
        if (level_conducting(PIN_LEVEL_SENSE_NEG)) {
            nh++;
        }
        level_idle();
        esp_rom_delay_us(LEVEL_GAP_US);
    }
    *pos_hits = ph;
    *neg_hits = nh;
}

bool hal_level_present(hal_level_id_t level)
{
    if (level == HAL_LEVEL_RESERVOIR) {
        /* Float switch closed to GND => water present. Adjust polarity to
         * match your switch wiring. */
        return gpio_get_level(PIN_LEVEL_RESERVOIR) == 0;
    }

    int select;
    switch (level) {
    case HAL_LEVEL_BREW:  select = LEVEL_SELECT_BREW;  break;
    case HAL_LEVEL_STEAM: select = LEVEL_SELECT_STEAM; break;
    default:
        return true; /* unknown probe: fail safe against overfilling */
    }

    /* Probe at two frequencies; require BOTH directions to conduct at BOTH.
     * Genuine water passes all four counts; one-directional leakage or a
     * fixed-frequency machine noise source does not. Relax to a single burst /
     * one direction here if bench calibration shows this is too strict. */
    int pf, nf, ps, ns;
    level_burst(select, LEVEL_DWELL_FAST_US, &pf, &nf);
    level_burst(select, LEVEL_DWELL_SLOW_US, &ps, &ns);
    level_idle();

    return pf >= LEVEL_WET_MIN_HITS && nf >= LEVEL_WET_MIN_HITS &&
           ps >= LEVEL_WET_MIN_HITS && ns >= LEVEL_WET_MIN_HITS;
}

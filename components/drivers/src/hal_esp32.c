#include "hal/hal.h"

#include "esp_log.h"

#include "hal/hal_display.h"
#include "hal/hal_flow.h"
#include "hal/hal_heater.h"
#include "hal/hal_input.h"
#include "hal/hal_level.h"
#include "hal/hal_pump.h"
#include "hal/hal_storage.h"
#include "hal/hal_temp.h"
#include "hal/hal_valve.h"

static const char *TAG = "hal";

espresso_result_t hal_init(void)
{
    espresso_result_t rc = ESPRESSO_OK;

    /* Bring up each subsystem; log but continue so the UI can still report a
     * sensor fault rather than failing to boot. */
#define INIT(call)                                              \
    do {                                                        \
        if ((call) != ESPRESSO_OK) {                            \
            ESP_LOGE(TAG, "%s failed", #call);                  \
            rc = ESPRESSO_ERR_STATE;                            \
        }                                                       \
    } while (0)

    INIT(hal_storage_init());
    INIT(hal_temp_init());
    INIT(hal_heater_init());
    INIT(hal_pump_init());
    INIT(hal_valve_init());
    INIT(hal_flow_init());
    INIT(hal_level_init());
    INIT(hal_input_init());
    INIT(hal_display_init());

#undef INIT
    return rc;
}

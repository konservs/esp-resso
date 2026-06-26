#include "hal/hal_time.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_ms_t hal_time_ms(void)
{
    return (esp_ms_t)(esp_timer_get_time() / 1000);
}

void hal_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

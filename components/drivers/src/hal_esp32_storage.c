#include "hal/hal_storage.h"

#include "nvs.h"

#define STORAGE_NAMESPACE "espresso"

espresso_result_t hal_storage_init(void)
{
    /* nvs_flash_init() is performed once in app_main before any HAL call. */
    return ESPRESSO_OK;
}

espresso_result_t hal_storage_save(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    esp_err_t err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK ? ESPRESSO_OK : ESPRESSO_ERR_STATE;
}

espresso_result_t hal_storage_load(const char *key, void *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    size_t sz = len;
    const esp_err_t err = nvs_get_blob(h, key, out, &sz);
    nvs_close(h);
    return (err == ESP_OK && sz == len) ? ESPRESSO_OK : ESPRESSO_ERR_STATE;
}

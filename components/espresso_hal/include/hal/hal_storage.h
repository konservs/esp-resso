/**
 * @file hal_storage.h
 * @brief Tiny key/value persistence for settings (NVS on the ESP32).
 */
#ifndef ESPRESSO_HAL_STORAGE_H
#define ESPRESSO_HAL_STORAGE_H

#include <stddef.h>

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the storage backend (nvs_flash on ESP32). */
espresso_result_t hal_storage_init(void);

/** Persist @p len bytes under @p key. */
espresso_result_t hal_storage_save(const char *key, const void *data, size_t len);

/**
 * @brief Load a previously saved blob.
 * @return ::ESPRESSO_OK on success, ::ESPRESSO_ERR_STATE if @p key is absent.
 */
espresso_result_t hal_storage_load(const char *key, void *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_STORAGE_H */

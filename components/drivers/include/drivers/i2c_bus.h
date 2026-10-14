/**
 * @file i2c_bus.h
 * @brief Single shared ESP32 I2C master bus for all on-board I2C peripherals.
 *
 * The SSD1306 OLED and the PCF8574 input expander sit on the same physical bus
 * (PIN_I2C_SDA / PIN_I2C_SCL). The ESP-IDF v5 I2C driver models this as one
 * bus handle with many device handles, so the bus must be created exactly once
 * and shared. This module owns that single bus.
 *
 * ::i2c_bus_init is idempotent and order-independent: whichever driver comes up
 * first creates the bus, the rest reuse it. Each device driver then calls
 * ::i2c_bus_handle and adds itself with i2c_master_bus_add_device().
 *
 * ## Recovery
 *
 * The panel and the expander hang off a long cable, and a jammed bus is a real
 * failure mode there: a slave interrupted mid-byte keeps holding SDA low, and a
 * brown-out at the far end drags both lines down through the slaves' protection
 * diodes. Neither clears by retrying the transfer — the IDF driver reports the
 * NACK, fails to see the bus go idle, and the caller loops on it forever.
 *
 * So a driver that keeps failing calls ::i2c_bus_recover instead of retrying,
 * and this module escalates: first a soft reset (peripheral FSM reset plus the
 * nine clock pulses that walk a stuck slave off SDA), then a full teardown and
 * rebuild of the bus. A rebuild needs every device off the bus first — the IDF
 * refuses to delete a bus that still has devices attached — so drivers register
 * a ::i2c_bus_client_t and this module detaches and re-attaches them around it.
 * That re-attach is also what re-runs each chip's init sequence, which is the
 * part that actually clears a confused panel.
 */
#ifndef ESPRESSO_DRIVERS_I2C_BUS_H
#define ESPRESSO_DRIVERS_I2C_BUS_H

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Device drivers that can register for recovery (OLED + expander, today). */
#define I2C_BUS_MAX_CLIENTS 4

/**
 * @brief Hooks a device driver provides so the bus can be rebuilt underneath it.
 *
 * Both hooks run with the bus lock held and must not call back into this
 * module. @c detach only has to drop the device handle; @c attach must re-add
 * the device *and* re-run the chip's initialisation, since a rebuild is only
 * useful if the peripheral comes back configured.
 */
typedef struct {
    const char *name;            /**< For logging, e.g. "ssd1306".            */
    esp_err_t (*detach)(void);   /**< Remove the device from the bus.         */
    esp_err_t (*attach)(void);   /**< Re-add the device and re-init the chip. */
} i2c_bus_client_t;

/**
 * @brief Bring up the shared I2C master bus on @p sda_gpio / @p scl_gpio.
 *
 * Safe to call multiple times. The first call creates the bus; later calls are
 * no-ops and return ESP_OK (a mismatched pin request returns ESP_ERR_INVALID_STATE
 * so a wiring mistake is caught rather than silently ignored).
 */
esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio);

/** @brief The shared bus handle, or NULL if ::i2c_bus_init has not succeeded. */
i2c_master_bus_handle_t i2c_bus_handle(void);

/**
 * @brief Register @p client so the bus can be torn down and rebuilt under it.
 *
 * @p client must have static lifetime; only the pointer is kept. Registering
 * the same client twice is a no-op, so this is safe to call from an init path
 * that may run more than once.
 */
esp_err_t i2c_bus_register_client(const i2c_bus_client_t *client);

/**
 * @brief Report that a transfer succeeded, clearing the recovery escalation.
 *
 * Drivers call this on every good transfer. It is what makes ::i2c_bus_recover
 * start again from a cheap soft reset next time, instead of staying escalated
 * for the rest of the run.
 */
void i2c_bus_report_ok(void);

/**
 * @brief Ask for the next escalation step of bus recovery.
 *
 * Paced internally, so a driver can call this on every failure without
 * thinking about it: calls that arrive too soon after the last attempt return
 * false having done nothing. The pacing backs off while failures continue, so
 * an unplugged cable costs one attempt every few seconds rather than one per
 * frame.
 *
 * @return true if a recovery step ran and succeeded.
 */
bool i2c_bus_recover(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_DRIVERS_I2C_BUS_H */

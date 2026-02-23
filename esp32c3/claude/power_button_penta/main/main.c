/**
 * main.c  –  Power Button Penta
 *
 * Flow:
 *   1. Initialise NVS (required by BT stack).
 *   2. Initialise TinyUSB HID keyboard.
 *   3. Initialise BLE GATT server ("Penta Power Btn").
 *   4. Configure automatic light sleep so idle current is minimal while
 *      still keeping the BLE radio and USB controller alive.
 *
 * When a BLE client (phone / Raspberry Pi) writes to the Wake characteristic,
 * ble_server.c calls usb_hid_send_wake_key() which sends a Space keypress
 * over USB to resume the host PC from S3 sleep.
 *
 * Light sleep notes for ESP32-C3:
 *   - The BLE LL uses its own sleep/wakeup schedule; light sleep is
 *     entered automatically between BLE events by the power-management
 *     driver when CONFIG_PM_ENABLE=y and CONFIG_FREERTOS_USE_TICKLESS_IDLE=y.
 *   - USB is kept alive by VBUS from the host; the host suspends VBUS
 *     during its own S3, but the PC BIOS/UEFI typically keeps USB powered
 *     on the header that the wake device is attached to.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "nvs_flash.h"

#include "usb_hid.h"
#include "ble_server.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    /* ── NVS ───────────────────────────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── USB HID ───────────────────────────────────────────────────────── */
    usb_hid_init();

    /* ── BLE GATT server ───────────────────────────────────────────────── */
    ble_server_init();

    /* ── Power management – automatic light sleep ──────────────────────── *
     * CPU runs at up to 160 MHz when active, drops to the minimum clock
     * (and enters light sleep) whenever FreeRTOS is idle.               */
    esp_pm_config_t pm_config = {
        .max_freq_mhz  = 80,   /* Lower max saves more power; 80 is safe */
        .min_freq_mhz  = 10,
        .light_sleep_enable = true,
    };
    ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Power management config failed (may need sdkconfig "
                      "options): %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Light sleep enabled");
    }

    ESP_LOGI(TAG, "Power Button Penta ready – advertising as 'Penta Power Btn'");

    /* Main loop – nothing to do; events are handled in BLE callbacks and
     * the TinyUSB task.  We vTaskDelay to let the idle task run (and
     * thus enter light sleep). */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/*
 * Penta Power Button — ESP32-C3 firmware
 *
 * BLE GATT server (NimBLE) + USB HID keyboard (TinyUSB)
 *
 * When a BLE client writes any non-zero byte to the Wake characteristic,
 * the device sends a single HID keystroke (F15) over USB to wake the PC
 * from S3 sleep.  The rest of the time the chip sits in automatic light
 * sleep, consuming ~2-3 mA, well within what the PC's suspended USB port
 * supplies (~500 mA budget, stays powered in S3 by default on ATX boards).
 *
 * Build requirements (ESP-IDF >= 5.1):
 *   - CONFIG_BT_NIMBLE_ENABLED=y
 *   - CONFIG_TINYUSB_HID_ENABLED=y
 *   - CONFIG_PM_ENABLE=y  (light sleep via power management)
 *
 * UUID layout (128-bit, random but stable — feel free to regenerate):
 *   Service   : 4FAFC201-1FB5-459E-8FCC-C5C9C331914B
 *   Wake Char : BEB5483E-36E1-4688-B7F5-EA07361B26A8
 */

#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* TinyUSB HID */
#include "tinyusb.h"
#include "class/hid/hid_device.h"

/* ─── Configuration ────────────────────────────────────────────────────────── */

#define DEVICE_NAME          "Power button Penta"   /* BLE advertised name    */
#define HID_WAKEKEY          HID_KEY_F15             /* keystroke sent to PC   */
#define KEYSTROKE_HOLD_MS    80                      /* key-down duration      */

/* Service UUID */
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5,
                     0xcc, 0x8f, 0x9e, 0x45, 0xb5, 0x1f,
                     0x01, 0xc2, 0xaf, 0x4f);

/* Wake characteristic UUID */
static const ble_uuid128_t wake_chr_uuid =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea,
                     0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36,
                     0x3e, 0x48, 0xb5, 0xbe);

static const char *TAG = "penta_pwrbtn";

/* ─── USB HID ──────────────────────────────────────────────────────────────── */

/*
 * Minimal HID report descriptor — boot-compatible keyboard.
 * This is what the PC sees over USB.
 */
static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

/* TinyUSB HID callbacks (required by the driver) */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
    /* We don't process LED reports (num-lock etc.) — ignore. */
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

/* USB device descriptor */
static const tusb_desc_device_t usb_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,   /* Espressif test VID — fine for personal use */
    .idProduct          = 0x4002,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

/* USB string descriptors */
static const char *usb_string_descriptor[] = {
    (char[]){0x09, 0x04},           /* 0: Language (English)  */
    "Espressif",                    /* 1: Manufacturer        */
    "Power button Penta",           /* 2: Product             */
    "PB-PENTA-001",                 /* 3: Serial              */
};

/* HID config string for TinyUSB init */
static const tinyusb_config_t tusb_cfg = {
    .device_descriptor        = &usb_device_descriptor,
    .string_descriptor        = usb_string_descriptor,
    .string_descriptor_count  = sizeof(usb_string_descriptor) /
                                sizeof(usb_string_descriptor[0]),
    .external_phy             = false,
    .configuration_descriptor = NULL,  /* use default single-config */
};

/* Send a single key press + release over USB HID */
static void usb_send_keystroke(uint8_t keycode)
{
    /* key down */
    uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, 0, keycodes);
    vTaskDelay(pdMS_TO_TICKS(KEYSTROKE_HOLD_MS));

    /* key up */
    memset(keycodes, 0, sizeof(keycodes));
    tud_hid_keyboard_report(0, 0, keycodes);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "HID keystroke sent (keycode 0x%02x)", keycode);
}

/* ─── BLE GATT ─────────────────────────────────────────────────────────────── */

static uint16_t wake_chr_val_handle;

/*
 * Called when the BLE client writes to the Wake characteristic.
 * Any non-zero single byte triggers the wakeup keystroke.
 */
static int wake_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len >= 1 && ctxt->om->om_data[0] != 0x00) {
            ESP_LOGI(TAG, "Wake command received over BLE → sending HID keystroke");
            usb_send_keystroke(HID_WAKEKEY);
        }
        return 0;
    }
    /* Reads return 0x00 */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t val = 0x00;
        int rc = os_mbuf_append(ctxt->om, &val, sizeof(val));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* Descriptor callback for 0x2901 User Description */
static int user_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    static const char desc[] = "Power button Penta";
    int rc = os_mbuf_append(ctxt->om, desc, strlen(desc));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/*
 * GATT service table.
 *
 * One service with one characteristic.
 * A User Description descriptor ("Power button Penta") is attached so that
 * nRF Connect / LightBlue / BLE Scanner on iOS and Android show a human-
 * readable label without needing to look up the UUID.
 */
static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Wake characteristic — write a non-zero byte to trigger */
                .uuid        = &wake_chr_uuid.u,
                .access_cb   = wake_chr_access_cb,
                .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle  = &wake_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        /* 0x2901 = Characteristic User Description */
                        .uuid      = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = user_desc_access_cb,
                    },
                    { 0 } /* terminator */
                },
            },
            { 0 } /* terminator */
        },
    },
    { 0 } /* terminator */
};

/* ─── BLE GAP / advertising ────────────────────────────────────────────────── */

static void start_advertising(void);

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "BLE client connected, handle=%d",
                     event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connection failed — restarting advertising");
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE client disconnected (reason %d) — restarting advertising",
                 event->disconnect.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising completed — restarting");
        start_advertising();
        break;

    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,   /* undirected connectable   */
        .disc_mode = BLE_GAP_DISC_MODE_GEN,   /* general discoverable     */
        .itvl_min  = BLE_GAP_ADV_ITVL_MS(500),
        .itvl_max  = BLE_GAP_ADV_ITVL_MS(1000),
    };

    /* Advertising data: flags + complete local name */
    struct ble_hs_adv_fields fields = {
        .flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name                  = (uint8_t *)DEVICE_NAME,
        .name_len              = strlen(DEVICE_NAME),
        .name_is_complete      = 1,
        .tx_pwr_lvl_is_present = 1,
        .tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO,
    };

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields error: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start error: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started: \"%s\"", DEVICE_NAME);
    }
}

/* ─── NimBLE host task ─────────────────────────────────────────────────────── */

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();          /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void ble_host_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void ble_host_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    start_advertising();
}

/* ─── app_main ─────────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Penta Power Button booting ===");

    /* NVS (required by BLE stack) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* ── USB HID init ── */
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB HID initialised");

    /*
     * ── Light sleep via ESP-IDF power management ──
     *
     * With CONFIG_PM_ENABLE=y and CONFIG_FREERTOS_USE_TICKLESS_IDLE=y the
     * chip automatically enters light sleep whenever FreeRTOS is idle.
     * BLE radio events act as wake sources transparently through NimBLE.
     * USB stays active because the PHY is fed from VBUS.
     */
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz       = 80,  /* reduce from 160 MHz default */
        .min_freq_mhz       = 10,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_cfg));
    ESP_LOGI(TAG, "Light sleep enabled (max 80 MHz / min 10 MHz)");

    /* ── BLE / NimBLE init ── */
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb    = ble_host_on_reset;
    ble_hs_cfg.sync_cb     = ble_host_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Register GATT services */
    int rc = ble_gatts_count_cfg(gatt_services);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_services);
    assert(rc == 0);

    /* Set GAP device name (shows up in iOS Bluetooth settings) */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    ble_svc_gap_init();
    ble_svc_gatt_init();

    nimble_port_freertos_init(nimble_host_task);

    /* app_main may return — everything runs in FreeRTOS tasks */
    ESP_LOGI(TAG, "Boot complete. Listening for BLE wake commands.");
}

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* USB HID */
#include "tusb.h"

static const char *TAG = "PENTA";

/* UUIDs */
static const ble_uuid128_t service_uuid =
    BLE_UUID128_INIT(0xab,0x90,0x78,0x56,0x34,0x12,0x34,0x12,
                     0x34,0x12,0x34,0x12,0x78,0x56,0x34,0x12);

static const ble_uuid128_t char_uuid =
    BLE_UUID128_INIT(0xcd,0x90,0x78,0x56,0x34,0x12,0x34,0x12,
                     0x34,0x12,0x34,0x12,0x78,0x56,0x34,0x12);

/* Forward */
static void send_wake_key(void);

/* GATT Write Callback */
static int gatt_write(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt,
                      void *arg)
{
    ESP_LOGI(TAG, "Wake command received");
    send_wake_key();
    return 0;
}

/* GATT DB */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &char_uuid.u,
                .access_cb = gatt_write,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0}
        }
    },
    {0}
};

/* BLE Advertise */
static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};

    const char *name = "PowerButton Penta";

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(0,NULL,BLE_HS_FOREVER,&params,NULL,NULL);
}

/* BLE Sync */
static void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0,NULL);
    advertise();
}

/* Host Task */
void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* USB HID Wake Key */
static void send_wake_key(void)
{
    if (!tud_hid_ready()) return;

    uint8_t keycode[6] = { HID_KEY_A };

    tud_hid_keyboard_report(0,0,keycode);
    vTaskDelay(pdMS_TO_TICKS(30));
    tud_hid_keyboard_report(0,0,NULL);

    ESP_LOGI(TAG,"USB Wake Sent");
}

/* HID Report Descriptor */
uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD()
};

/* USB descriptors */
tusb_desc_device_t const desc_device =
{
  .bLength = sizeof(desc_device),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = 0,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = 0xCafe,
  .idProduct = 0x4010,
  .bcdDevice = 0x0100,
  .iManufacturer = 0x01,
  .iProduct = 0x02,
  .bNumConfigurations = 0x01
};

const uint8_t *tud_descriptor_device_cb(void){ return (uint8_t*)&desc_device; }
const uint8_t *tud_hid_descriptor_report_cb(uint8_t itf){ return desc_hid_report; }

/* Power Save Task */
void sleep_task(void *arg)
{
    while (1) {
        esp_light_sleep_start();
    }
}

/* MAIN */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* USB */
    tinyusb_config_t tusb_cfg = {0};
    tinyusb_driver_install(&tusb_cfg);

    /* BLE */
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set("PowerButton Penta");

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(host_task);

    /* Sleep loop */
    xTaskCreate(sleep_task,"sleep",2048,NULL,1,NULL);
}

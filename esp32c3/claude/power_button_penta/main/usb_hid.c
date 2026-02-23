/**
 * usb_hid.c
 *
 * Configures TinyUSB as a minimal USB HID keyboard.
 * usb_hid_send_wake_key() presses and releases the Space bar (HID keycode
 * 0x2C) which reliably wakes a PC from S3 suspend over USB.
 *
 * The USB remote wake-up feature is also enabled in the descriptor so the
 * host PC keeps the bus powered during suspend and the device can signal
 * a wake even without a software keystroke – but the keystroke ensures the
 * desktop is also un-locked/un-blanked.
 */

#include "usb_hid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char *TAG = "USB_HID";

/* ── HID report descriptor – boot-compatible keyboard ─────────────────── */
static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

/* ── TinyUSB string descriptors ─────────────────────────────────────────── */
static const char *string_desc[] = {
    (const char[]){0x09, 0x04},    /* 0: supported language: English */
    "Anthropic-DIY",               /* 1: manufacturer */
    "Penta Power Button",          /* 2: product */
    "PB-001",                      /* 3: serial */
};

/* ── TinyUSB device descriptor ──────────────────────────────────────────── */
static const tusb_desc_device_t device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,   /* Espressif test VID */
    .idProduct          = 0x1001,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

/* ── TinyUSB HID callbacks (required by TinyUSB) ────────────────────────── */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

/* ── TinyUSB task ────────────────────────────────────────────────────────── */
static void usb_task(void *arg)
{
    while (1) {
        tud_task();   /* TinyUSB device task */
        vTaskDelay(1);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void usb_hid_init(void)
{
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor      = &device_descriptor,
        .string_descriptor      = string_desc,
        .string_descriptor_count = sizeof(string_desc) / sizeof(string_desc[0]),
        .external_phy           = false,
        .configuration_descriptor = NULL,  /* use class-default */
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    /* Run the TinyUSB stack in its own task */
    xTaskCreate(usb_task, "usb_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB HID keyboard initialised");
}

void usb_hid_send_wake_key(void)
{
    /* Wait until the USB host is ready */
    int retries = 100;
    while (!tud_hid_ready() && retries-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!tud_hid_ready()) {
        ESP_LOGW(TAG, "USB HID not ready – skipping key press");
        return;
    }

    /* Press Space (keycode 0x2C) with no modifiers */
    uint8_t keycode[6] = {HID_KEY_SPACE, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, 0x00, keycode);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Release all keys */
    tud_hid_keyboard_report(0, 0x00, NULL);
    ESP_LOGI(TAG, "Wake key sent");
}

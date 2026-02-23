# Conversation with Gemini

## Question

```
Bluetooth power button with esp32c3

I have a PC that automatically goes into S3 sleep/suspend after a certain time of inactivity. I want to wake it up with a keystroke. I want to use the esp32c3 as HID to send this signal over USB to the PC, while also being powered by the PC. It works with a wireless keyboard. I assume that the idle power consumption of the esp32c3 is low enough to fulfill the same purpose. I want the bluetooth module of the esp32c3 to be listening to a wake signal to send a wake key signal to the pc. The wake signal should be send from a smartphone (iOS and Android) and a Raspberry Pi 4. What are some steps to consider, which apps would work and serve the purpose?
```

## Gemini said

Waking a PC from S3 sleep using an ESP32-C3 acting as a USB HID (Human Interface Device) is a clever solution, especially when standard Wake-on-LAN (WoL) isn't reliable. The ESP32-C3 is particularly well-suited for this because it has native USB support and low power consumption.

Here is a breakdown of how to achieve this, the challenges you'll face, and the tools to use.

1. Hardware & Power Considerations
Your assumption about power is correct, but with a few "gotchas":

PC Power in S3: Most modern PCs keep the USB ports powered in S3 sleep (often called "Always-on USB" or "Sleep-and-Charge"). You must ensure this is enabled in your BIOS/UEFI settings.

ESP32-C3 Power Consumption:

Active BLE: While listening for a Bluetooth signal, the ESP32-C3 will draw roughly 80-100 mA.

Light Sleep: To save power while still maintaining a Bluetooth connection, you can use Modem-sleep or Light-sleep, which can drop consumption significantly (down to ~1-3 mA), but this requires more complex programming to keep the BLE timing windows open.

Standard USB Port Limit: A standard USB 2.0 port provides up to 500mA, so even at peak power, the ESP32-C3 is well within safe limits.

2. Firmware Logic (The ESP32 Side)
You will need to use a library that supports USB HID (not Bluetooth HID, since the ESP is plugged into the PC) and BLE (Bluetooth Low Energy) for the wake signal.

USB HID Library: Use the USBHIDKeyboard library (standard in the Arduino ESP32 core for C3/S3).

BLE Library: Use NimBLE-Arduino. It is more memory-efficient than the standard BLE library and handles connections better.

The Workflow: 1.  The ESP32-C3 starts as a BLE Server and a USB Keyboard.
2.  It listens for a specific value written to a BLE Characteristic (e.g., sending the byte 0x01).
3.  When received, the ESP32 calls Keyboard.press(KEY_F15) or Keyboard.wakeup().

[!IMPORTANT]
Remote Wakeup: In your USB descriptor, the "Remote Wakeup" bit must be set. The PC must also be configured in Device Manager (Windows) to "Allow this device to wake the computer" for the emulated keyboard.

3. Implementation Tools & Apps
For Smartphone (iOS & Android)
Since you are building a custom trigger, you don't need a "Keyboard" app on the phone; you need a BLE Terminal or a custom dashboard app to send a data packet to the ESP32.

nRF Connect for Mobile (Nordic Semiconductor): The "Swiss Army Knife" of BLE. You can manually connect to your ESP32 and write a value to the wake characteristic. Best for testing.

LightBlue: Similar to nRF Connect, very reliable on iOS.

Blynk: If you want a pretty "Wake PC" button on your home screen. You can set up a BLE project where a button press sends a command to the ESP32.

For Raspberry Pi 4
The Pi can act as a BLE Central to trigger the ESP32.

Tool: bluetoothctl (Command line) or a Python script using the bleak library.

Python Example:

Python
# Simple logic to send wake signal
import asyncio
from bleak import BleakClient

address = "XX:XX:XX:XX:XX:XX" # ESP32 MAC
WAKE_CHAR_UUID = "your-characteristic-uuid"

async def wake_pc():
    async with BleakClient(address) as client:
        await client.write_gatt_char(WAKE_CHAR_UUID, b'\x01')

asyncio.run(wake_pc())
4. Steps to Consider
BIOS Check: Confirm "USB Wake Support" is Enabled.

USB HID Setup: Flash a basic "Hello World" USB Keyboard sketch to the C3 to ensure the PC recognizes it as an input device.

BLE Characteristcs: Define a simple GATT Service on the ESP32 with one "Write" characteristic for the wake command.

Windows/Linux Config: In your OS, find the "ESP32 Keyboard" in the device list and ensure the power management settings allow it to wake the system.

Would you like me to provide a starter Arduino code template for the ESP32-C3 that combines the USB Keyboard and NimBLE server?

You said
Can you write me a program for the esp32c3 that I can use with Visual Studio Code and ESP-IDF to upload to the MCU? The esp32c3 should be in light sleep mode, and GATT server make it easy to discover the service from iOS and Android, maybe have a descriptor visible "Power button Penta" for my Penta-GPU server? And currently without the need of a password, I'm not sending personal data, just switching on a machine.

Gemini said
This is a sophisticated use case. Using ESP-IDF with TinyUSB and NimBLE (a lightweight Bluetooth stack) is the most efficient way to achieve this on an ESP32-C3.

To make this work, the ESP32-C3 will act as a USB HID Composite Device. When it receives a specific Bluetooth write command, it will issue a Remote Wakeup signal to the PC over the USB bus.

Prerequisites
VS Code with the ESP-IDF Extension installed.

Your ESP32-C3 connected via its native USB pins (GPIO 18 = D-, GPIO 19 = D+).

Step 1: Create the Project and Add Dependencies
Open your ESP-IDF terminal in VS Code and run:

Bash
idf.py create-project power_button_penta
cd power_button_penta
idf.py add-dependency "espressif/esp_tinyusb^1.4.3"
Step 2: Configure the Project (sdkconfig)
You must enable Power Management and Bluetooth Sleep in the configuration menu (idf.py menuconfig):

Component config → ESP System Settings → Support for power management: Enabled.

Component config → Bluetooth → NimBLE Options → Enable sleep settings: Enabled.

Component config → TinyUSB Stack: Configure as HID Keyboard and ensure Remote Wakeup is enabled.

Step 3: The Source Code (main.c)
This code initializes the BLE GATT server with the name "Power button Penta" and sets up the USB HID driver.

C
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_pm.h"

// Bluetooth Includes
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// USB Includes
#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char *TAG = "PentaWake";
uint16_t wake_handle;

// --- USB HID Configuration ---
static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1))
};

const tinyusb_config_t tusb_cfg = {
    .device_descriptor = NULL,
    .string_descriptor = NULL,
    .external_phy = false,
    .configuration_descriptor = NULL,
};

// --- BLE GATT Logic ---
static int ble_svc_wake_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGI(TAG, "Wake signal received via BLE!");
        
        // 1. Send USB Remote Wakeup to PC
        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        
        // 2. Send a dummy keystroke just in case the PC is awake but screen is off
        uint8_t keycode[6] = {HID_KEY_F15}; 
        tud_hid_keyboard_report(1, 0, keycode);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_hid_keyboard_report(1, 0, NULL); // Release key
        
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID128_DECLARE(0xDE, 0xAD, 0xBE, 0xEF, 0xBA, 0xBE, 0xCA, 0xFE, 0xDE, 0xAD, 0xBE, 0xEF, 0xBA, 0xBE, 0xCA, 0xFE),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0xFF01),
          .access_cb = ble_svc_wake_cb,
          .flags = BLE_GATT_CHR_F_WRITE,
          .val_handle = &wake_handle},
         {0}}},
    {0}};

void ble_app_advertise(void) {
    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t *)"Power button Penta";
    adv_fields.name_len = strlen("Power button Penta");
    adv_fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&adv_fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_HS_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
}

void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {
    // 1. Initialize NVS (Required for BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Initialize USB HID
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // 3. Initialize BLE
    nimble_port_init();
    ble_svc_gap_device_name_set("Power button Penta");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    nimble_port_freertos_init(ble_host_task);
    ble_app_advertise();

    // 4. Power Management (Light Sleep)
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_LOGI(TAG, "Penta-GPU Wake Controller Started.");
}
4. How to use it
From Android/iOS
Download nRF Connect for Mobile.

Scan for devices; you will see "Power button Penta".

Connect to it.

Find the Service with UUID DEADBEEF... and the Characteristic FF01.

Write any value (e.g., 0x01) to that characteristic.

Your PC will receive a USB "Remote Wakeup" event and an F15 keypress.

From Raspberry Pi 4
You can trigger this from the command line using gatttool or a simple Python script using bleak:

Python
import asyncio
from bleak import BleakClient

address = "XX:XX:XX:XX:XX:XX" # Replace with ESP32-C3 MAC
CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"

async def main():
    async with BleakClient(address) as client:
        await client.write_gatt_char(CHAR_UUID, b'\x01')
        print("Wake signal sent to Penta server.")

asyncio.run(main())
Important PC Settings
Windows: Open Device Manager → Keyboards → [Your ESP32 Device] → Power Management. Check "Allow this device to wake the computer."

BIOS: Ensure "Wake on USB" or "USB Resume" is enabled in your BIOS/UEFI settings.

Would you like me to show you how to set up the CMakeLists.txt file to ensure the TinyUSB and NimBLE components are linked correctly?

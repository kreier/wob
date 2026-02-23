#pragma once
#include <stdbool.h>

/**
 * Initialise the BLE GATT server.
 * Registers a custom service with a single "Wake" characteristic.
 * Writing any value to the characteristic triggers a USB HID wake keystroke.
 */
void ble_server_init(void);

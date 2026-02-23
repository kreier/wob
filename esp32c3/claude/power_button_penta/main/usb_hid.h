#pragma once

/**
 * Initialise TinyUSB as a HID keyboard device.
 * Must be called before ble_server_init().
 */
void usb_hid_init(void);

/**
 * Send a single key press + release that wakes a sleeping PC.
 * Uses the "Wake" key (HID usage 0x00 / modifier only pulse is enough on most
 * hosts; we send a short Space press as a universal fallback).
 */
void usb_hid_send_wake_key(void);

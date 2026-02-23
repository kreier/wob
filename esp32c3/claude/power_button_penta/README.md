# Power Button Penta — ESP32-C3 BLE → USB HID Wake

Wake your Penta-GPU server from S3 sleep via BLE from an iPhone, Android phone, or Raspberry Pi 4.

---

## How it works

```
Phone / RPi 4
  └─ BLE write to "Wake" characteristic
       └─ ESP32-C3 (BLE GATT server + USB HID keyboard)
            └─ USB HID Space keypress → PC wakes from S3
```

The ESP32-C3 sits on the USB header (or a USB port) of the PC. It is powered
by the PC's USB supply, which most BIOSes keep live during S3 sleep. It
advertises as **"Penta Power Btn"** with a GATT User Description of
**"Power button Penta"** on the single Wake characteristic.

---

## Hardware requirements

| Item | Notes |
|------|-------|
| ESP32-C3 dev board (e.g. ESP32-C3-DevKitM-1) | With native USB (pins 18/19) |
| USB-A cable | Powers the ESP32-C3 from the PC and carries HID |
| PC BIOS setting | "USB wake from S3" or "USB power in sleep" — enable it |

> **Internal USB header**: If you want a clean install, a USB A-male to 9-pin
> internal header adapter lets you plug the ESP32-C3 into the motherboard
> header directly.

---

## Build & Flash (VS Code + ESP-IDF ≥ 5.2)

1. **Install prerequisites**: [ESP-IDF VS Code Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
2. Open the project folder in VS Code.
3. Set the correct serial port in `.vscode/settings.json` (`idf.portWin` / `idf.portUnix`).
4. Run **Terminal → Run Task → ESP-IDF: Flash & Monitor**.

Or from a terminal:

```bash
. $IDF_PATH/export.sh          # activate ESP-IDF environment
idf.py set-target esp32c3
idf.py build flash monitor -p /dev/ttyUSB0
```

---

## BLE service layout

| | UUID | Properties |
|-|------|------------|
| Service | `0x00FF` | – |
| Wake characteristic | `0xFF01` | WRITE, WRITE_NO_RSP |
| User Description | `0x2901` | READ → `"Power button Penta"` |

Write **any byte** to `0xFF01` to trigger the wake keystroke.

---

## Sending the wake signal

### iOS — LightBlue / nRF Connect
Both apps are free and available on the App Store.

| App | Steps |
|-----|-------|
| **nRF Connect** | Scan → connect to *Penta Power Btn* → expand service `0x00FF` → tap ↑ (write) on characteristic `0xFF01` → write `01` (hex) |
| **LightBlue** | Scan → connect → find `0xFF01` → tap *Write new value* → `0x01` |

For a one-tap shortcut on iOS 16+: use **Shortcuts app** with the
*Bluetooth Write* action (available via the *BlueApp* shortcut community, or
build your own with the BLE Write shortcut action). Assign it to a Home Screen
button or Back Tap.

### Android — nRF Connect / BLE Scanner
- **nRF Connect** (Nordic Semiconductor, free on Play Store): same steps as iOS above.
- **BLE Scanner** (Bluepixel Technologies): scan, connect, locate `0xFF01`, send `01`.

For an Android home-screen widget: **BLExplore** or **Macrodroid** + nRF
Connect intent can automate a one-tap wake button widget.

### Raspberry Pi 4 — Python script

Install dependencies:
```bash
pip install bleak
```

Create `wake_penta.py`:
```python
import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME   = "Penta Power Btn"
WAKE_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"

async def wake():
    print("Scanning for Penta Power Btn …")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device is None:
        print("Device not found.")
        return
    async with BleakClient(device) as client:
        await client.write_gatt_char(WAKE_CHAR_UUID, bytes([0x01]),
                                     response=False)
        print("Wake signal sent!")

asyncio.run(wake())
```

Run: `python3 wake_penta.py`

Add to cron or a systemd service / button press script as needed.

---

## Power consumption notes

- **Active BLE advertising** (20–40 ms interval): ~10–15 mA
- **Light sleep between BLE events**: ~1–3 mA average depending on advertising interval
- USB-powered from the PC during S3: most ATX PSUs supply ≥100 mA on USB
  standby power — this is well within budget.
- The BLE advertising interval in `ble_server.c` can be increased
  (`adv_int_min/max`) to 160–500 ms to further reduce average current at the
  cost of slightly slower discovery time.

---

## BIOS settings to check

- **ErP / EuP Lot 6**: **Disable** — this cuts all standby USB power.
- **USB Wake Support / Resume by USB Device**: **Enable**.
- **Power On By PCI-E/USB**: **Enable**.

---

## Security note

No pairing or authentication is implemented. Anyone within BLE range can send
the wake signal. This is acceptable for waking a locked desktop — an attacker
only wakes the machine, they still need credentials to log in. If you later
want to add security, implement BLE bonding/pairing (ESP-IDF Bluedroid supports
this) or a simple challenge–response write protocol.

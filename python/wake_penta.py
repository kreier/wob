# Script for the Raspberry Pi 4 to wake the Penta-GPU server

import asyncio
from bleak import BleakClient

DEVICE_ADDRESS = "XX:XX:XX:XX:XX:XX"  # your ESP32-C3's BLE MAC
CHAR_UUID = "your-characteristic-uuid"

async def wake():
    async with BleakClient(DEVICE_ADDRESS) as client:
        await client.write_gatt_char(CHAR_UUID, b'\x01')

asyncio.run(wake())

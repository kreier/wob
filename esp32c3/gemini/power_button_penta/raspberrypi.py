import asyncio
from bleak import BleakClient

address = "XX:XX:XX:XX:XX:XX" # Replace with ESP32-C3 MAC
CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"

async def main():
    async with BleakClient(address) as client:
        await client.write_gatt_char(CHAR_UUID, b'\x01')
        print("Wake signal sent to Penta server.")

asyncio.run(main())

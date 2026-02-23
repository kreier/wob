# Conversation with LLMs

## Project outline

Bluetooth power button with esp32c3

I have a PC that automatically goes into S3 sleep/suspend after a certain time of inactivity. I want to wake it up with a keystroke. I want to use the esp32c3 as HID to send this signal over USB to the PC, while also being powered by the PC. It works with a wireless keyboard. I assume that the idle power consumption of the esp32c3 is low enough to fulfill the same purpose. I want the bluetooth module of the esp32c3 to be listening to a wake signal to send a wake key signal to the pc. The wake signal should be send from a smartphone (iOS and Android) and a Raspberry Pi 4. What are some steps to consider, which apps would work and serve the purpose?

## Code refinements

Can you write me a program for the esp32c3 that I can use with Visual Studio Code and ESP-IDF to upload to the MCU? The esp32c3 should be in light sleep mode, and GATT server make it easy to discover the service from iOS and Android, maybe have a descriptor visible "Power button Penta" for my Penta-GPU server? And currently without the need of a password, I'm not sending personal data, just switching on a machine.

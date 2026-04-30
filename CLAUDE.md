# CLAUDE.md - ESPHome HaLow Component

## Project Overview
ESPHome external component for Wi-Fi HaLow (IEEE 802.11ah) using XIAO ESP32-S3 + WIO-WM6180 (Morse Micro MM6108).

## Hardware
- MCU: XIAO ESP32-S3 (Xtensa, USB JTAG at /dev/ttyACM0)
- HaLow: WIO-WM6180 hat, MM6108 SoC, SPI interface
- AP: GL-iNet HaLowLink 2

## Key Files
- `HALOW_NOTES.md` — Detailed hardware notes, pin mappings, SDK info, project plan

## Build & Flash
- Uses ESP-IDF (not Arduino) for MM-IoT-SDK compatibility
- Flash target: `/dev/ttyACM0` (ESP32-S3 USB JTAG)
- `esptool.py --port /dev/ttyACM0 --chip esp32s3`

## Important Notes
- MM6108 communicates over SPI, not UART/AT commands
- BCF (Board Config File) binary must be loaded onto MM6108 at init
- Pin mapping needs verification against actual HaLow Hat schematic
- BUSY pin may not be wired — disable power save as workaround

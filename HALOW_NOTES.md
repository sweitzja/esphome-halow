# ESPHome HaLow Component - Project Notes

## Goal
Build an ESPHome external component that enables IEEE 802.11ah (Wi-Fi HaLow) connectivity
via the Seeed XIAO HaLow Hat (WIO-WM6180) on an XIAO ESP32-S3.

## Hardware

### XIAO ESP32-S3
- Espressif ESP32-S3 (Xtensa dual-core)
- USB-C with native USB JTAG/serial debug
- VID:PID 303a:1001
- Appears as `/dev/ttyACM0` in WSL2 (via usbipd)

### XIAO HaLow Hat (WIO-WM6180)
- Morse Micro MM6108 SoC (IEEE 802.11ah)
- Sub-1 GHz (902-928 MHz in North America)
- Interface: **SPI** from ESP32-S3 to MM6108
- 3.3V operation
- Quectel FGH100M-H module inside

### GL-iNet HaLowLink 2
- Wi-Fi HaLow access point (MM8108-based)
- Up to 1 km range, 1000 devices
- OpenWrt 23.05
- 750-950 MHz, channel widths 1/2/4/8 MHz
- Also bridges to 2.4 GHz Wi-Fi

## Pin Mapping: ESP32-S3 ↔ MM6108 (via HaLow Hat)

Based on Seeed documentation and community sources (needs verification):

| Function   | ESP32-S3 GPIO | XIAO Pin | Notes                    |
|------------|---------------|----------|--------------------------|
| SPI SCK    | GPIO 7        | D8       | SPI clock                |
| SPI MISO   | GPIO 8        | D9       | Master In Slave Out      |
| SPI MOSI   | GPIO 9        | D10      | Master Out Slave In      |
| SPI CS     | GPIO 3        | D3/A3    | Chip select (needs verify)|
| RESET_N    | GPIO 4        | D4/A4    | Module reset (active low)|
| BUSY/IRQ   | GPIO 1        | D1/A1    | Interrupt (may not be wired)|
| WAKE       | GPIO 2        | D2/A2    | Wake signal (needs verify)|

**WARNING**: Pin mapping has conflicting sources. Must verify against actual
HaLow Hat schematic or by probing. The Seeed mm-iot-esp32 sdkconfig is
the authoritative source.

## SDK / Firmware

### Morse Micro MM-IoT-SDK
- ESP-IDF based SDK for MM6108/MM8108
- Upstream: https://github.com/MorseMicro/mm-iot-esp32
- Seeed fork: https://github.com/Seeed-Studio/mm-iot-esp32
- Requires ESP-IDF v5.1.1+ (Seeed) or v5.2.2+ (upstream)
- Board Config Files (BCF): binary blobs loaded onto MM6108 at init
  - Located in `bcf/quectel/` directory (e.g., bcf_fgh100maamd.bin)

### Morse Firmware (BCF files)
- https://github.com/MorseMicro/morse-firmware/tree/main/bcf/quectel
- FGH100M variants: bcf_fgh100maamd.bin, bcf_fgh100mabmd.bin, etc.

### Related Projects
- **Xiao-Halow-to-WiFi-Bridge**: https://github.com/gtgreenw/Xiao-Halow-to-WiFi-Bridge
  - Existing project doing similar ESP32-S3 + WM6180 work
  - Good reference for pin configs and initialization

## Project Plan

### Phase 1: Hardware Verification (current)
1. ✅ Connect ESP32-S3 via USB serial
2. ⬜ Verify SPI communication with MM6108
3. ⬜ Scan for HaLow networks (find HaLowLink 2)

### Phase 2: Network Connectivity
4. ⬜ Connect to HaLowLink 2 AP
5. ⬜ Test IP connectivity (ping, iperf)
6. ⬜ Characterize range and throughput

### Phase 3: ESPHome Integration
7. ⬜ Create ESPHome external component structure
8. ⬜ Implement SPI driver for MM6108
9. ⬜ Expose HaLow as network interface
10. ⬜ Add configuration options (SSID, password, channel, etc.)
11. ⬜ Test with Home Assistant

## Key Challenges
- MM-IoT-SDK is ESP-IDF based; ESPHome uses Arduino or ESP-IDF framework
- BCF binary blobs need to be included and loaded at runtime
- SPI communication is complex (not simple AT commands)
- BUSY pin may not be wired on HaLow Hat - need to handle power save carefully
- Stack size requirements for Morse Micro SDK (similar to BMV080 experience)

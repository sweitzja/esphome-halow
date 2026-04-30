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

**VERIFIED** from Seeed's mm-iot-esp32 fork Kconfig defaults
(https://github.com/Seeed-Studio/mm-iot-esp32/blob/main/framework/mm_shims/Kconfig):

| Function   | ESP32-S3 GPIO | XIAO Pin | Notes                          |
|------------|---------------|----------|--------------------------------|
| SPI SCK    | GPIO 7        | D8       | SPI clock                      |
| SPI MISO   | GPIO 8        | D9       | Master In Slave Out            |
| SPI MOSI   | GPIO 9        | D10      | Master Out Slave In            |
| SPI CS     | GPIO 4        | D4/A4    | Chip select                    |
| SPI IRQ    | GPIO 3        | D3/A3    | Out-of-band interrupt (data ready)|
| RESET_N    | GPIO 1        | D1/A1    | Module reset (active low)      |
| WAKE       | GPIO 2        | D2/A2    | Wake signal                    |
| BUSY       | GPIO 5        | D5/A5    | Rising edge interrupt          |

### BCF (Board Configuration File)
- Default: `bcf_mf16858_us.mbin` (for WM6180 / FGH100M-H module)
- Firmware: `mm6108.mbin`
- Chip type: MM6108

## SDK / Firmware

### Morse Micro MM-IoT-SDK
- ESP-IDF based SDK for MM6108/MM8108
- Upstream: https://github.com/MorseMicro/mm-iot-esp32
- Seeed fork: https://github.com/Seeed-Studio/mm-iot-esp32 (used for this project)
- Requires ESP-IDF v5.1.1 (Seeed fork pinned to this version)
- Local installs:
  - ESP-IDF: `~/esp/esp-idf-v5.1.1/`
  - MM-IoT-SDK: `~/esp/mm-iot-esp32/`
- Board Config Files (BCF): binary blobs loaded onto MM6108 at init
  - Located in `framework/morsefirmware/` directory
- MM6108 has NO persistent firmware — ESP32 must load fw+bcf over SPI every boot

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
2. ✅ Verify SPI communication with MM6108 (SDIO registers readable)
3. ✅ Scan for HaLow networks — found HaLowLink 2!
   - SSID: halowlink2-627b, BSSID: 50:2e:91:d2:c9:e4
   - RSSI: -41 dBm, BW: 8 MHz, Security: SAE (WPA3)
   - Morse FW v1.13.1, morselib v2.6.4-esp32, chip ID 0x306

### Phase 2: Network Connectivity
4. ✅ Connect to HaLowLink 2 AP
   - WPA3-SAE auth successful with `your-password-here`
   - Link up in ~8 seconds from boot
   - ARP test packet sent successfully
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

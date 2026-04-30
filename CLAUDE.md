# CLAUDE.md - ESPHome HaLow Component

## Project Overview
ESPHome external component for Wi-Fi HaLow (IEEE 802.11ah) using ESP32-S3 + Morse Micro MM6108.
Provides a drop-in `mm_halow:` YAML block that replaces `wifi:` for long-range sub-GHz networking.

## Repository Layout
```
esphome-halow/
├── components/mm_halow/       # ESPHome external component
│   ├── __init__.py            # YAML config schema + code generation
│   ├── mm_halow_component.h   # Component class (inherits esphome::Component)
│   └── mm_halow_component.cpp # MM-IoT-SDK bridge (setup/loop/callbacks)
├── spi_probe/                 # PlatformIO sketch: raw SPI probe of MM6108
├── sta_connect/               # ESP-IDF example: WPA3-SAE connection test
├── iperf/                     # ESP-IDF example: full LWIP + DHCP + iperf
├── example.yaml               # Sample ESPHome configuration
├── HALOW_NOTES.md             # Detailed hardware notes and project history
└── README.md                  # User-facing documentation
```

## Hardware
- **Host MCU**: Seeed XIAO ESP32-S3 (Xtensa dual-core, 8MB PSRAM, USB JTAG)
- **HaLow Module**: WIO-WM6180 hat (Morse Micro MM6108 SoC, Quectel FGH100M-H)
- **Access Point**: GL-iNet HaLowLink 2 (MM8108, WPA3-SAE, 802.11ah)
- **Serial port**: `/dev/ttyACM0` (needs `usbipd attach --wsl` from Windows, `chmod 666`)

## Pin Mapping (VERIFIED from Seeed Kconfig)
SCK=GPIO7, MISO=GPIO8, MOSI=GPIO9, CS=GPIO4, IRQ=GPIO3, RESET=GPIO1, WAKE=GPIO2, BUSY=GPIO5

## SDK Dependencies
- **ESP-IDF v5.1.1**: `~/esp/esp-idf-v5.1.1/` (Seeed fork pinned to this version)
- **MM-IoT-SDK**: `~/esp/mm-iot-esp32/` (Seeed fork)
- **Firmware files**: `framework/morsefirmware/mm6108.mbin` + `bcf_mf16858_us.mbin`

## Build Commands (ESP-IDF examples, not ESPHome)
```bash
export IDF_PATH=$HOME/esp/esp-idf-v5.1.1
export ESPTOOLS=$HOME/.espressif/tools
export PATH="$ESPTOOLS/xtensa-esp32s3-elf/esp-12.2.0_20230208/xtensa-esp32s3-elf/bin:$HOME/.local/bin:$IDF_PATH/tools:$PATH"
IDF_PYTHON_ENV_PATH=$(ls -d $HOME/.espressif/python_env/idf5.1_* | head -1)
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
export MMIOT_ROOT=$HOME/esp/mm-iot-esp32
cd <example_dir>
idf.py -DCOUNTRY_CODE=US set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash
```

## MM-IoT-SDK API Flow (what the component wraps)
1. `mmhal_init()` -- SPI bus + GPIO init
2. `mmwlan_init()` -- WLAN subsystem
3. `mmwlan_set_channel_list()` -- regulatory domain (country code)
4. `mmwlan_register_link_state_cb()` -- link up/down notifications
5. `mmwlan_boot()` -- loads mm6108.mbin + BCF over SPI (~500ms)
6. `mmipal_init()` -- LWIP stack with DHCP
7. `mmwlan_sta_enable()` -- starts WPA3-SAE connection
8. Link up callback fires (~8s), DHCP completes
9. `mmipal_get_ip_config()` -- poll for IP address

## Key Technical Facts
- MM6108 has NO persistent firmware; loaded over SPI every boot
- Uses SDIO-over-SPI protocol (not plain SPI registers)
- SPI runs at 40 MHz after init
- MM-IoT-SDK creates its own FreeRTOS tasks for SPI and WLAN
- Requires `CONFIG_FREERTOS_HZ=1000` and `CONFIG_FREERTOS_TIMER_TASK_PRIORITY=10`
- morselib is a prebuilt .a (libmorse_nocrypto.a) for xtensa-lx7
- BCF file determines regulatory domain and module hardware config
- WPA3-SAE is the default (and primary) security mode for HaLow
- The BUSY pin may not be wired on all boards; power save should be disabled

## Verified Results
- MM6108 chip ID: 0x306, FW v1.13.1, morselib v2.6.4-esp32
- MAC: a8:dd:9f:4d:c6:01
- AP: halowlink2-627b (BSSID 50:2e:91:d2:c9:e4), RSSI -41 dBm, 8 MHz BW
- DHCP IP: 192.168.12.164, gateway 192.168.12.1
- Link-up time: ~8-10 seconds from cold boot

## Current Status
- Hardware verification: COMPLETE
- Network connectivity: COMPLETE (scan, connect, IP, iperf)
- ESPHome component: SCAFFOLDED (needs build integration with MM-IoT-SDK)
- Remaining: link morselib into ESPHome build, embed .mbin firmware files, test end-to-end

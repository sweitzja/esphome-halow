# CLAUDE.md - ESPHome HaLow Component

## Project Overview
ESPHome external component for Wi-Fi HaLow (IEEE 802.11ah) using ESP32-S3 + Morse Micro MM6108.
Provides a drop-in `halow:` YAML block that replaces `wifi:` for long-range sub-GHz networking.
Zero third-party dependencies beyond the auto-downloaded Morse Micro SDK.

## Repository Layout
```
esphome-halow/
├── components/halow/       # ESPHome external component
│   ├── __init__.py            # YAML config schema + code generation + auto-download SDK
│   ├── halow_component.h   # Component class with sensor pointers
│   ├── halow_component.cpp # MM-IoT-SDK bridge with state machine + mDNS + reconnect
│   ├── network_wrap.cpp       # Linker --wrap overrides for network::is_connected()
│   └── pre_build.py           # PlatformIO script: firmware blob linking (3 strategies)
├── spi_probe/                 # PlatformIO sketch: raw SPI probe of MM6108
├── sta_connect/               # ESP-IDF example: WPA3-SAE connection test
├── iperf/                     # ESP-IDF example: full LWIP + DHCP + iperf
├── example.yaml               # Sample ESPHome configuration (all features)
├── HALOW_NOTES.md             # Detailed engineering reference
└── README.md                  # User-facing documentation with full sensor docs
```

## SDK Management
The component auto-downloads the upstream Morse Micro MM-IoT-SDK to
`~/.esphome/mm-iot-esp32/` on first compile. **No patches needed.**

- **Repo**: https://github.com/MorseMicro/mm-iot-esp32 (upstream, native IDF 5.5.2)
- **Override path**: set `mm_iot_sdk_path` in YAML to use a local clone
- FW v1.17.6, morselib v2.10.4-esp32
- Sub-bands disabled by default for 8 MHz operation (`sub_bands: false`)

## Hardware
- **Host MCU**: Seeed XIAO ESP32-S3 (USB JTAG at /dev/ttyACM0 via usbipd)
- **HaLow Module**: WIO-WM6180 hat (MM6108 SoC, Quectel FGH100M-H)
- **Access Point**: GL-iNet HaLowLink 2 (WPA3-SAE, bridged via WAN port)

## Pin Mapping (VERIFIED)
SCK=GPIO7, MISO=GPIO8, MOSI=GPIO9, CS=GPIO4, IRQ=GPIO3, RESET=GPIO1, WAKE=GPIO2, BUSY=GPIO5

## Build & Flash
```bash
esphome compile example.yaml
esphome upload example.yaml --device /dev/ttyACM0
# OTA over HaLow:
esphome upload example.yaml --device 192.168.1.86
```

## MM-IoT-SDK API Flow (what the component wraps)
0. `esp_register_shutdown_handler()` -- register OTA cleanup (sta_disable→shutdown→deinit→periph_reset)
1. `periph_module_reset(PERIPH_SPI2_MODULE)` -- reset SPI hardware (belt + suspenders)
2. `spi_bus_free(SPI2_HOST)` -- free stale driver state
3. Hardware reset MM6108 via RESET_N pin toggle
4. `mmhal_init()` -- SPI bus + GPIO init
5. `mmwlan_init()` -- WLAN subsystem
6. `mmwlan_set_channel_list()` -- regulatory domain (country code)
7. `mmwlan_boot()` -- loads mm6108.mbin + BCF over SPI (~700ms)
8. `mmwlan_set_power_save_mode(PS_DISABLED)` -- keep radio awake
9. `mmwlan_set_subbands_enabled(false)` -- force 8 MHz bandwidth
10. `mmipal_init()` -- LWIP stack (DHCP or static IP)
11. `mmipal_set_link_status_callback()` -- reconnection via FreeRTOS timer
12. `mmwlan_sta_enable()` -- starts WPA3-SAE connection

## Critical Technical Notes
- OTA shutdown handler: `mmwlan_sta_disable→shutdown→deinit→periph_reset` before restart — essential for clean OTA reboot
- `mmwlan_set_subbands_enabled(false)` — forces 8 MHz; without this, upstream SDK defaults to 2 MHz
- FW and BCF must be from the same SDK version (mixing causes `FW manifest pointer not set` crash)
- `LWIP_NETIF_LINK_CALLBACK=1` set via `-D` flag (no Kconfig in IDF 5.5)
- Power save MUST be disabled (BUSY pin broken in MM firmware)
- `netif_set_link_up()` forced after IP acquisition with `LOCK_TCPIP_CORE()` wrapper
- Reconnection via FreeRTOS timer (NOT main loop — SDK wedges if called from main loop)
- TX bytes counted via `netif->linkoutput` hook; RX bytes via `--wrap=tcpip_input` (SDK calls `tcpip_input()` directly, bypassing `netif->input`)
- UMAC stats via `mmwlan_get_umac_stats()` — exposes TX/RX queue drops, CCMP failures, HW restarts
- Channel scan via `mmwlan_scan_request()` while connected — `noise_dbm` in scan results is the only source of noise floor data (no SNR API for active connections)
- 902-928 MHz ISM band shared with LoRa, smart meters, YoLink — channel selection matters (channel 44/924 MHz often cleanest in US)

## Exposed Sensors (22 total) + 1 Button
**Signal**: rssi (dBm), link_quality (Excellent/Good/Fair/Poor/Critical)
**Radio**: mcs (0-7), bandwidth (MHz), tx_success_rate (%)
**Traffic**: tx_pps, rx_pps (packets per second), tx_kbps, rx_kbps (kilobits per second)
**Diagnostics**: noise_floor (dBm, from scan), tx_frames_dropped, rx_frames_dropped, ccmp_failures, hw_restarts
**Network**: ip_address, gateway_address, subnet_mask, connected_ssid, bssid, mac_address, firmware_version
**Channel Scan**: scan_channel (button), scan_results (JSON text sensor)

## Working Test Results
```
FW: 1.17.6, morselib: 2.10.4-esp32, chip: 0x306
MAC: A8:DD:9F:4D:C6:01, RSSI: -39 dBm
IP: 192.168.1.86, GW: 192.168.1.1 (bridged via HaLowLink 2)
Ping: 10/10, 0% loss, 7-11ms (power save disabled)
MCS: 7, Bandwidth: 8 MHz (sub-bands disabled), TX success: 100%
Channel: 44 (924 MHz) — cleanest in US ISM band, avoids 915 MHz LoRa interference
Noise floor: -92 dBm (channel 44), vs -60 dBm (channel 28 near YoLink/LoRa devices)
mDNS: halow-test.local + _esphomelib._tcp registered
HA: Home Assistant 2026.4.2 connected, all 22 sensors + scan button visible
OTA: 1.05MB in 8-13s over HaLow, reboot + reconnect clean
Reconnect: FreeRTOS timer, 16s range walk recovery, no crash
Link Quality: Excellent→Poor→Excellent (MCS 7→0→7 observed in real time)
Heap: stable, zero leak over 20 min soak
SDK: auto-downloaded from MorseMicro/mm-iot-esp32 (upstream, no patches)
Build: zero manual setup, ~140s clean compile
```

# CLAUDE.md - ESPHome HaLow Component

## Project Overview
ESPHome external component for Wi-Fi HaLow (IEEE 802.11ah) using ESP32-S3 + Morse Micro MM6108.
Provides a drop-in `mm_halow:` YAML block that replaces `wifi:` for long-range sub-GHz networking.

## Repository Layout
```
esphome-halow/
├── components/mm_halow/       # ESPHome external component
│   ├── __init__.py            # YAML config schema + code generation
│   ├── mm_halow_component.h   # Component class with sensor pointers
│   ├── mm_halow_component.cpp # MM-IoT-SDK bridge with state machine
│   └── pre_build.py           # PlatformIO script: firmware blob linking
├── spi_probe/                 # PlatformIO sketch: raw SPI probe of MM6108
├── sta_connect/               # ESP-IDF example: WPA3-SAE connection test
├── iperf/                     # ESP-IDF example: full LWIP + DHCP + iperf
├── example.yaml               # Sample ESPHome configuration (all features)
├── HALOW_NOTES.md             # Detailed engineering reference
└── README.md                  # User-facing documentation for ESPHome devs
```

## Which SDK to Use
**Seeed fork** (`~/esp/mm-iot-esp32/`) — patched for IDF 5.5.2 compatibility.
The upstream SDK v2.10.4 compiles but its BCF files lack US regulatory config.

### Patches applied to Seeed SDK
1. `idf_component.yml` files: `==5.1.1` -> `>=5.1.1` (6 files under framework/)
2. `mm_shims/mmosal_shim_freertos_esp32.c` line 127: added `SECONDARY` arg to `ESP_SYSTEM_INIT_FN`

See HALOW_NOTES.md "Required Patches" section for exact commands.

## Hardware
- **Host MCU**: Seeed XIAO ESP32-S3 (USB JTAG at /dev/ttyACM0 via usbipd)
- **HaLow Module**: WIO-WM6180 hat (MM6108 SoC, Quectel FGH100M-H)
- **Access Point**: GL-iNet HaLowLink 2 (WPA3-SAE, 192.168.12.1)

## Pin Mapping (VERIFIED)
SCK=GPIO7, MISO=GPIO8, MOSI=GPIO9, CS=GPIO4, IRQ=GPIO3, RESET=GPIO1, WAKE=GPIO2, BUSY=GPIO5

## Build & Flash
```bash
esphome compile example.yaml
esphome upload example.yaml --device /dev/ttyACM0
```

## MM-IoT-SDK API Flow (what the component wraps)
1. `mmhal_init()` -- SPI bus + GPIO init
2. `mmwlan_init()` -- WLAN subsystem
3. `mmwlan_set_channel_list()` -- regulatory domain (country code)
4. `mmwlan_register_link_state_cb()` -- link up/down notifications
5. `mmwlan_boot()` -- loads mm6108.mbin + BCF over SPI (~700ms)
6. `mmipal_init()` -- LWIP stack (DHCP or static IP)
7. `mmwlan_sta_enable()` -- starts WPA3-SAE connection
8. Poll `mmipal_get_link_state()` + `mmipal_get_ip_config()` for IP

## Critical Firmware/BCF Facts
- FW and BCF MUST be from the same SDK version (mixing causes `FW manifest pointer not set` crash)
- Seeed BCF `bcf_mf16858_us.mbin` works for US regulatory
- Upstream BCF `bcf_mf16858.mbin` loads but does not connect (wrong freq band)
- Quectel BCFs from morse-firmware repo need `convert-bin-to-mbin.py` conversion
- The `mmwlan_register_link_state_cb()` may not fire; use `mmipal_get_link_state()` polling as fallback
- `LWIP_NETIF_LINK_CALLBACK=1` must be set via `-D` flag (no Kconfig in IDF 5.5)

## mDNS Integration
mDNS works via a fake `esp_netif_obj` wrapper around mmipal's raw LWIP netif:
- Find netif by matching MAC in `netif_list`
- Allocate `esp_netif_obj` with `lwip_netif` pointer set
- `mdns_free()` + `mdns_init()` to reinitialize after ESPHome's failed attempt
- `mdns_register_netif()` + `mdns_netif_action(ENABLE_IP4)`
- Requires: `CONFIG_MDNS_PREDEF_NETIF_STA/AP/ETH=n`
- Requires: `-I<framework-espidf>/components/esp_netif/lwip` for internal header

## Working Test Results
```
FW: 1.13.1, morselib: 2.6.4-esp32, chip: 0x306
MAC: A8:DD:9F:4D:C6:01, RSSI: -35 dBm
IP: 192.168.12.164, GW: 192.168.12.1
BSSID: 50:2E:91:D2:C9:E4, BCF: bcf_mf16858_us.mbin
mDNS: halow-test.local registered
ESPHome API (6053) + OTA (3232) running
All sensors (RSSI, TX/RX packets, IP, gateway, BSSID, MAC, FW version) publishing
```

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

## SDK Management
The component auto-downloads the upstream Morse Micro MM-IoT-SDK to
`~/.esphome/mm-iot-esp32/` on first compile. **No patches needed.**

- **Repo**: https://github.com/MorseMicro/mm-iot-esp32 (upstream, native IDF 5.5.2)
- **Override path**: set `mm_iot_sdk_path` in YAML to use a local clone
- FW v1.17.6, morselib v2.10.4-esp32
- Sub-bands must be disabled for 8 MHz operation (`sub_bands: false`, the default)

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
- **Power save MUST be disabled** for reliable inbound connectivity (ARP/ping/API)
- Must force `netif_set_link_up()` after IP acquisition — mmipal callback doesn't fire on IDF 5.5

## SPI Peripheral Reset (Critical for OTA)
`periph_module_reset(PERIPH_SPI2_MODULE)` must be called before `mmhal_init()`.
Without this, OTA reboot crash-loops because SPI2 hardware retains stale state
from the previous session. Also call `spi_bus_free(SPI2_HOST)` to free driver state.

## Power Save
- `mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED)` called after boot
- Default PS_ENABLED causes radio to sleep between beacons, missing inbound ARP/ICMP
- With PS disabled: 10/10 pings, 0% loss, 7-11ms latency
- BUSY pin is broken in Morse Micro firmware (confirmed by MM community)
- Workaround exists: `MORSE_CMD_PARAM_ID_WAKE_ACTION_GPIO` for alternate wake pin
- Monitor https://github.com/MorseMicro/firmware_binaries/releases/ for BUSY fix
- Future: TWT requires working BUSY pin or the GPIO workaround

## HaLowLink 2 Setup
- **WAN port** to LAN (not LAN port — wizard bridges WAN↔HaLow)
- Wizard: "HaLow devices get IP on your existing router's network" (bridge mode)
- Must disable masquerade: `uci set firewall.wlan.masq=0; uci commit firewall`
- Device IP from main DHCP server (same subnet as LAN)

## mDNS Integration
mDNS works via a fake `esp_netif_obj` wrapper around mmipal's raw LWIP netif:
- Find netif by matching MAC in `netif_list`
- Allocate `esp_netif_obj` with `lwip_netif` pointer set
- `mdns_free()` + `mdns_init()` to reinitialize after ESPHome's failed attempt
- `mdns_register_netif()` + `mdns_netif_action(ENABLE_IP4)`
- Requires: `CONFIG_MDNS_PREDEF_NETIF_STA/AP/ETH=n`
- Requires: `-I<framework-espidf>/components/esp_netif/lwip` for internal header

## Network Provider (--wrap)
ESPHome's `network::is_connected()` doesn't know about mm_halow. We use
`--wrap` on 3 mangled C++ symbols to intercept and check our component first.
Without this, the API server disconnects all clients with "Network down".
File: `network_wrap.cpp`

## Reconnection (FreeRTOS Timer)
- `mmipal_link_status_callback` detects LINK_DOWN, starts 15s FreeRTOS timer
- Timer runs `do_halow_reconnect()`: `sta_disable()` + `sta_enable()` from timer task
- **Must run from timer task, NOT main loop** — SDK wedges if called from main loop
- `mmwlan_shutdown()`/`mmwlan_boot()` crashes WDT — don't use for reconnection
- RSSI-triggered disconnect causes false positives — don't use
- Verified: 16s recovery from range walk, no crash

## Working Test Results
```
FW: 1.13.1, morselib: 2.6.4-esp32, chip: 0x306
MAC: A8:DD:9F:4D:C6:01, RSSI: -34 dBm
IP: 192.168.1.86, GW: 192.168.1.1 (bridged via HaLowLink 2)
Ping: 10/10, 0% loss, 7-11ms (power save disabled)
BSSID: 50:2E:91:D2:C9:E4, BCF: bcf_mf16858_us.mbin
mDNS: halow-test.local + _esphomelib._tcp registered
HA: Home Assistant 2026.4.2 connected, all entities visible
OTA: 1.05MB in 8-13s, reboot + reconnect works
Reconnect: FreeRTOS timer, 16s range walk recovery, no crash
Link Quality: Excellent→Poor→Excellent (MCS 7→0→7 observed)
Heap: stable, zero leak
SDK: auto-downloaded from MorseMicro/mm-iot-esp32 (upstream, no patches)
Build: zero manual setup, ~140s clean compile
```

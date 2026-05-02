# Engineering Notes - ESPHome Wi-Fi HaLow Component

## Overview

IEEE 802.11ah (Wi-Fi HaLow) operates in sub-1 GHz spectrum (902-928 MHz in North America)
for long-range, low-power IoT connectivity. This project wraps the Morse Micro MM-IoT-SDK
into an ESPHome external component, providing a `mm_halow:` config block analogous to `wifi:`
or `ethernet:` (W5500).

## Hardware Stack

### Morse Micro MM6108 SoC
- Single-chip 802.11ah solution with integrated MAC/PHY/radio
- Sub-1 GHz: 902-928 MHz (US/AU), 863-868 MHz (EU), 916.5-927.5 MHz (JP)
- Channel widths: 1, 2, 4, 8 MHz
- Max data rate: 32.5 Mbps (8 MHz, MCS10)
- Typical range: 100m-1km (depending on environment and data rate)
- Interface to host: SDIO-over-SPI (up to 50 MHz, typically 40 MHz)
- No persistent firmware -- host loads FW + BCF over SPI at every boot
- Security: WPA3-SAE (primary), OWE, Open
- Chip ID observed: 0x306

### Seeed XIAO HaLow Hat (WIO-WM6180)
- Contains Quectel FGH100M-H module (MM6108 inside)
- Designed to stack on XIAO ESP32-S3
- 3.3V operation
- SMA antenna connector (external antenna)
- Pin mapping defined in Seeed's mm-iot-esp32 Kconfig fork

### Seeed XIAO ESP32-S3
- ESP32-S3 (QFN56), revision v0.2
- Dual-core Xtensa LX7 @ 240 MHz
- 8 MB PSRAM, 8 MB flash (GD chip)
- USB-C with native USB JTAG/serial debug (VID:PID 303a:1001)
- MAC: d8:3b:da:45:55:08

### GL-iNet HaLowLink 2 (Test AP)
- Model: MM-HL2-EXT (HW v0.1)
- MAC: 50:2E:91:D2:C9:E4
- MM8108-based (802.11ah AP + 2.4 GHz Wi-Fi bridge)
- OpenWrt 23.05
- Default IP: 192.168.12.1
- DHCP range: 192.168.12.x
- Security: WPA3-SAE
- Management: root / (see device label) at 192.168.12.1

## Pin Mapping: ESP32-S3 to MM6108

**Source**: [Seeed mm-iot-esp32 Kconfig](https://github.com/Seeed-Studio/mm-iot-esp32/blob/main/framework/mm_shims/Kconfig)

These are the Kconfig *defaults* in the Seeed fork, which differ from the upstream
Morse Micro defaults. Verified working with XIAO ESP32-S3 + XIAO HaLow Hat.

| Function   | ESP32-S3 GPIO | XIAO Silk | Kconfig Key     | Notes                          |
|------------|---------------|-----------|-----------------|--------------------------------|
| SPI SCK    | GPIO 7        | D8        | MM_SPI_SCK      | SPI clock                      |
| SPI MISO   | GPIO 8        | D9        | MM_SPI_MISO     | Master In Slave Out            |
| SPI MOSI   | GPIO 9        | D10       | MM_SPI_MOSI     | Master Out Slave In            |
| SPI CS     | GPIO 4        | D4        | MM_SPI_CS       | Chip select (active low)       |
| SPI IRQ    | GPIO 3        | D3        | MM_SPI_IRQ      | Out-of-band data-ready interrupt|
| RESET_N    | GPIO 1        | D1        | MM_RESET_N      | Module reset (active low)      |
| WAKE       | GPIO 2        | D2        | MM_WAKE         | Wake signal                    |
| BUSY       | GPIO 5        | D5        | MM_BUSY         | Rising edge interrupt          |

**Upstream (Morse Micro) defaults are different**: SCK=12, MOSI=11, MISO=13, CS=10, IRQ=5,
RESET=3, WAKE=8, BUSY=9. These are for the Morse Micro EKH08 dev board, not the XIAO hat.

## Firmware and Board Configuration

### Binary File Formats
The MM-IoT-SDK uses `.mbin` files (a proprietary packed format). The `morse-firmware` repo
at https://github.com/MorseMicro/morse-firmware uses `.bin` files (ELF RISC-V format).
To use `.bin` BCF files from morse-firmware with the MM-IoT-SDK, you must convert them:

```bash
python3 framework/tools/buildsystem/convert-bin-to-mbin.py INPUT.bin -o OUTPUT.mbin
```

### BCF Files (Board Configuration Files)
BCFs contain hardware-specific and regulatory configuration. Using the wrong BCF
will either crash (`FW manifest pointer not set`) or silently fail to connect
(radio configured for wrong frequency band).

**Seeed SDK (`~/esp/mm-iot-esp32/framework/morsefirmware/`):**
| File                    | Module          | Region | Status    |
|-------------------------|-----------------|--------|-----------|
| `bcf_mf16858_us.mbin`  | FGH100M-H       | US     | **Working** |
| `bcf_mf08651_us.mbin`  | MF08651          | US     | Untested  |
| `bcf_mf08551.mbin`     | MF08551          | Multi  | Untested  |
| `bcf_mf08251.mbin`     | MF08251          | Multi  | Untested  |
| `bcf_mf03120.mbin`     | MF03120          | Multi  | Untested  |

**Upstream SDK (`~/esp/mm-iot-esp32-upstream/framework/morsefirmware/mm6108/bcfs/`):**
| File                    | Module          | Region | Status    |
|-------------------------|-----------------|--------|-----------|
| `bcf_mf16858.mbin`     | FGH100M-H       | **Not US** | Loads FW but cannot connect |
| `bcf_mf08651_us.mbin`  | MF08651          | US     | Not tested with FGH100M-H |
| `bcf_mf08651_jp.mbin`  | MF08651          | JP     | Untested  |

**Quectel BCFs (from `morse-firmware` repo, need `.bin` -> `.mbin` conversion):**
| File                     | Module          | Notes |
|--------------------------|-----------------|-------|
| `bcf_fgh100mhaamd.bin`  | FGH100M-**H**   | Correct module match; converts but does not connect (may lack US regulatory) |
| `bcf_fgh100maamd.bin`   | FGH100M-A       | Different module variant |
| `bcf_fgh100mabmd.bin`   | FGH100M-AB      | Different module variant |
| `bcf_fgh100mjaamd.bin`  | FGH100M-J       | Different module variant |

### Firmware Files
| File         | SDK Version    | FW Version | morselib        | Status |
|--------------|----------------|------------|-----------------|--------|
| `mm6108.mbin` (upstream) | v2.10.4-esp32 | 1.17.6 | 2.10.4-esp32 | **Working** (with sub-bands disabled) |
| `mm6108.mbin` (Seeed)   | v2.6.4-esp32 | 1.13.1 | 2.6.4-esp32  | Working (legacy) |

**Critical**: Firmware and BCF must be from the same SDK version. Mixing causes
`FW manifest pointer not set` crash.

### How Firmware Loading Works
1. ESP32 holds MM6108 in reset (RESET_N low)
2. ESP32 releases reset, asserts WAKE
3. ESP32 initializes SPI bus at 400 kHz, sends init clocks
4. SDIO-over-SPI initialization (CMD0, CMD5, CMD52)
5. MM-IoT-SDK loads `mm6108.mbin` via multi-byte SPI writes
6. MM-IoT-SDK loads BCF via multi-byte SPI writes
7. MM6108 boots its internal firmware, SPI clock increases to 40 MHz
8. Total boot time: ~700ms (to mmwlan_sta_enable call)
9. WPA3-SAE connection completes ~8-10s after boot

## SDK History

The component originally used the Seeed fork (v2.6.4) which required two patches for
ESP-IDF 5.5.2 compatibility. The upstream MM-IoT-SDK v2.10.4 was found to work after
two key fixes:
1. `periph_module_reset(PERIPH_SPI2_MODULE)` — fixes SPI reinit after OTA/soft reboot
2. `mmwlan_set_subbands_enabled(false)` — forces 8 MHz operation (upstream defaults to
   sub-band support which drops to 2 MHz)

The component now uses the **upstream SDK directly** with no patches needed.

## MM-IoT-SDK Architecture

### Repository Structure (Seeed fork)
```
mm-iot-esp32/
├── framework/
│   ├── morselib/          # Prebuilt binary library
│   │   ├── include/       # Public API headers (mmwlan.h, mmipal.h, etc.)
│   │   └── lib/
│   │       └── esp32-xtensa-lx7/
│   │           └── libmorse_nocrypto.a   # ~2.5MB static lib
│   ├── mm_shims/          # ESP-IDF HAL: SPI, GPIO, FreeRTOS glue
│   │   ├── Kconfig        # Pin definitions (THIS IS WHERE PINS ARE SET)
│   │   ├── mmhal.c        # SPI bus init, firmware loading (Seeed name)
│   │   ├── wlan_hal.c     # WLAN HAL bindings (Seeed name)
│   │   ├── mmhal_wlan_binaries.c  # References embedded .mbin.o objects
│   │   ├── mm6108.mbin.o          # Pre-built firmware object (xtensa ELF)
│   │   └── bcf_mf16858_us.mbin.o  # Pre-built BCF object (xtensa ELF)
│   ├── src/
│   │   ├── mmipal/        # IP abstraction (LWIP netif, DHCP)
│   │   ├── mmiperf/       # iperf implementation
│   │   ├── mmpktmem/      # Packet memory manager
│   │   └── mmutils/       # OS abstraction, config store
│   ├── morsefirmware/     # .mbin firmware files (source for the .o objects)
│   └── tools/buildsystem/ # convert-bin-to-mbin.py and other tools
├── examples/
│   ├── scan/              # Network scan
│   ├── sta_connect/       # Basic STA connection
│   ├── iperf/             # Full LWIP + DHCP + iperf
│   └── porting_assistant/ # Hardware verification tool
└── documentation.html     # Full API docs
```

### Key API Headers
- `mmwlan.h` -- WLAN control (boot, connect, scan, TX/RX)
- `mmipal.h` -- IP layer (init, DHCP, get_ip_config, get_ip6_config)
- `mmhal.h`  -- Hardware abstraction (init, SPI, GPIO)
- `mmosal.h` -- OS abstraction (tasks, semaphores, timers)
- `mmwlan_regdb.def` -- Regulatory database (Seeed: inline header, upstream: separate component)

### ESP-IDF Component Dependencies
When building as an ESP-IDF component, the SDK requires:
- `driver` (SPI, GPIO) — or `esp_driver_spi`/`esp_driver_gpio` on IDF 5.3+
- `freertos`
- `lwip` (with `LWIP_NETIF_STATUS_CALLBACK=1` and `LWIP_NETIF_LINK_CALLBACK=1`)
- `mbedtls` (for crypto_mbedtls_mm.c shim)
- `spi_flash`, `app_update`, `log`

### Required sdkconfig Settings
```
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_TIMER_TASK_PRIORITY=10    # Higher than MMOSAL_TASK_PRI_HIGH
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
CONFIG_MBEDTLS_NIST_KW_C=y
CONFIG_LWIP_NETIF_STATUS_CALLBACK=y       # Required by mmipal for DHCP callbacks
# LWIP_NETIF_LINK_CALLBACK=1              # Set via -D flag (no Kconfig in IDF 5.5)
```

## Available Metrics API

### Signal and Connection
- `mmwlan_get_rssi()` -> int32_t (dBm, INT32_MIN on error)
- `mmwlan_get_bssid(uint8_t *bssid)` -> status
- `mmwlan_get_aid()` -> uint16_t (Association ID, 0 if not associated)
- `mmwlan_get_sta_state()` -> enum (DISABLED=0, CONNECTING=1, CONNECTED=2)
- `mmwlan_get_mac_addr(uint8_t *mac)` -> status
- `mmwlan_get_version(struct mmwlan_version *)` -> fw version, morselib version, chip id

### IP Stack
- `mmipal_get_ip_config(struct mmipal_ip_config *)` -> ip_addr, netmask, gateway_addr
- `mmipal_get_link_packet_counts(uint32_t *tx, uint32_t *rx)` -> packet counters
- `mmipal_get_link_state()` -> MMIPAL_LINK_UP or MMIPAL_LINK_DOWN
- `mmipal_get_dns_server(uint8_t index, mmipal_ip_addr_t addr)` -> DNS servers

### Advanced (not exposed in ESPHome component)
- `mmwlan_get_duty_cycle_stats()` -> target duty cycle, burst timing
- `mmwlan_get_rc_stats()` -> per-rate TX counts and success rates
- `mmwlan_get_umac_stats()` -> queue stats, drops, reorder buffer, CCMP failures
- `mmwlan_get_morse_stats()` -> raw transceiver stats (opaque binary blob)
- `mmwlan_get_bcf_metadata()` -> board config version, description

### Security Types
```c
enum mmwlan_security_type { MMWLAN_OPEN, MMWLAN_OWE, MMWLAN_SAE };
```

### Reconnection Flow
```c
mmwlan_sta_disable();   // Brings link down, returns MMWLAN_SUCCESS or MMWLAN_SHUTDOWN_BLOCKED
// Then re-call:
mmwlan_sta_enable(&sta_args, sta_status_cb);  // Starts new connection
```

### Link State Detection
The `mmwlan_register_link_state_cb()` callback may not fire reliably on all SDK versions.
Poll `mmipal_get_link_state()` as a fallback for reliable link detection:
```c
bool link_is_up = s_link_up || (mmipal_get_link_state() == MMIPAL_LINK_UP);
```

## mDNS Integration (solved)

mmipal creates a raw LWIP `struct netif`, NOT an `esp_netif_t`. ESP-IDF's espressif/mdns
component requires `esp_netif_t*` for `mdns_register_netif()`. The LWIP netif is
static/private inside mmipal with no public getter.

### Solution: fake `esp_netif_obj` wrapper

After the HaLow link comes up and an IP is acquired, we:

1. **Find mmipal's LWIP netif** by iterating `netif_list` (LWIP global) and matching
   against the HaLow MAC address from `mmwlan_get_mac_addr()`.

2. **Allocate a minimal `esp_netif_obj`** struct (defined in the ESP-IDF internal header
   `esp_netif_lwip_internal.h`) and set `lwip_netif` to point to the found netif.
   The `esp_netif_get_ip_info()` function — which mDNS calls internally — reads
   IP/netmask/gateway directly from this LWIP netif pointer, so no other fields
   need to be populated.

3. **Reinitialize mDNS**: call `mdns_free()` (cleans up ESPHome's earlier failed init
   attempt) then `mdns_init()`. Set hostname via `mdns_hostname_set()`.

4. **Register and enable**: call `mdns_register_netif()` with our fake `esp_netif_t*`,
   then `mdns_netif_action(MDNS_EVENT_ENABLE_IP4)` and `MDNS_EVENT_ANNOUNCE_IP4`.

### Required sdkconfig for mDNS

Without these, `mdns_init()` tries to register WiFi/ETH event handlers that fail
because no standard WiFi or Ethernet driver is active:

```
CONFIG_MDNS_PREDEF_NETIF_STA=n
CONFIG_MDNS_PREDEF_NETIF_AP=n
CONFIG_MDNS_PREDEF_NETIF_ETH=n
```

### Required build flag

The `esp_netif_lwip_internal.h` header is not on the default include path:

```
-I~/.platformio/packages/framework-espidf/components/esp_netif/lwip
```

### Stability note

The `esp_netif_obj` struct layout is defined in an ESP-IDF internal header. It has been
stable since ESP-IDF v4.x through v5.5.2. If it changes in a future IDF version, the
fake wrapper would need updating. The struct is only used as a pointer container — mDNS
reads `lwip_netif` and calls `esp_netif_get_ip_info()` which reads IP from the LWIP netif
directly.

## ESPHome Integration Architecture

### Component Design
```
components/mm_halow/
├── __init__.py              # CONFIG_SCHEMA, to_code(), build flags, auto-download SDK
├── mm_halow_component.h     # Class declaration with sensor pointers
├── mm_halow_component.cpp   # MM-IoT-SDK bridge with state machine + mDNS
├── network_wrap.cpp         # Linker --wrap overrides for network::is_connected()
└── pre_build.py             # PlatformIO script: firmware blob linking (3 strategies)
```

### State Machine
```
setup() ok ──► CONNECTING ─── STA connected + valid IP ──► CONNECTED
                   ▲  timeout 60s                              │
                   │                                           │ mmipal LINK_DOWN
               STOPPED ◄──────────────────────────────────────┘
                   │  wait 5s                                  │
                   └──► CONNECTING                   FreeRTOS timer (15s)
                                                         │
                                                    sta_disable()
                                                    sta_enable()
                                                         │
                                                    mmipal LINK_UP
                                                         │
                                                    ──► CONNECTED
```

### Reconnection Architecture (FreeRTOS Timer)
Modeled after the Xiao-Halow-to-WiFi-Bridge project. **Critical**: the `sta_disable()`
and `sta_enable()` calls must run from a FreeRTOS timer task, NOT from the ESPHome
main loop. The 15-second delay lets the SDK clean up its internal state.

1. `mmipal_link_status_callback()` fires `MMIPAL_LINK_DOWN`
2. Callback starts a 15-second one-shot FreeRTOS timer (`pdFALSE`)
3. Timer fires `do_halow_reconnect()` from the timer service task
4. `mmwlan_sta_disable()` — cleans up the old association
5. `mmwlan_sta_enable()` — starts fresh WPA3-SAE association
6. On success, `mmipal_link_status_callback()` fires `MMIPAL_LINK_UP`
7. If `sta_enable` fails, timer restarts for another 15s attempt

**What doesn't work** (lessons learned):
- Calling `sta_disable`/`sta_enable` from the main loop → SDK wedges
- Calling `mmwlan_shutdown()` + `mmwlan_boot()` → WDT crash (SPI can't reinit)
- RSSI-triggered disconnect → false triggers, crash loops from premature reconnect
- Traffic watchdog alone → too slow (60s) or false positives

Logs `=== LINK LOST ===` and `=== LINK RECOVERED === after Xs downtime`.

### Network Provider Integration
ESPHome's `network::is_connected()` only checks USE_WIFI/USE_ETHERNET/etc.
We use linker `--wrap` to intercept three mangled C++ symbols:
- `_ZN7esphome7network12is_connectedEv` (is_connected)
- `_ZN7esphome7network16get_ip_addressesEv` (get_ip_addresses)
- `_ZN7esphome7network15get_use_addressEv` (get_use_address)

Each wrapper checks `global_mm_halow_component` first, then falls through to the
original implementation. Without this, the API server disconnects all clients with
"Network down" because it thinks there's no network.

### SDK Auto-Download
The upstream Morse Micro MM-IoT-SDK is auto-downloaded from
https://github.com/MorseMicro/mm-iot-esp32 to `~/.esphome/mm-iot-esp32/`
on first compile. **No patches needed** — the upstream SDK natively supports
ESP-IDF 5.5.2. Users can override with `mm_iot_sdk_path` in YAML.

### Sub-Band Configuration
The upstream SDK v2.10.4 enables sub-band support by default, which allows the
rate controller to drop to 1-2 MHz channels. This drastically reduces throughput
and RSSI compared to 8 MHz operation. The component disables sub-bands by default
(`sub_bands: false`). Users who need maximum range at the expense of throughput
can enable them with `sub_bands: true`.

### Build System
- `add_idf_component(path=...)` for morselib, mm_shims, mmipal, mmutils, mmpktmem,
  mmregdb
- `add_idf_sdkconfig_option()` for Kconfig (pins, FreeRTOS, BCF, chip type, LWIP)
- `pre_build.py` handles firmware blob linking with 3 fallback strategies:
  1. **ninja**: runs CMake custom targets (objcopy .mbin -> .o)
  2. **copy**: copies pre-built .o files from SDK mm_shims/ directory
  3. **objcopy**: generates .o directly from .mbin source files
- Auto-detects BCF name from Kconfig choice (Seeed) or string (upstream)
- Uses pre-patched Seeed MM-IoT-SDK v2.6.4 fork (IDF 5.5.2 compatible)

### Sensors
All sensors are optional YAML config entries within the `mm_halow:` block:

**Numeric sensors** (`sensor.sensor_schema()`):
| Config Key       | Unit | Device Class      | Source API |
|------------------|------|-------------------|------------|
| `rssi`           | dBm  | signal_strength   | `mmwlan_get_rssi()` |
| `tx_packets`     | —    | total_increasing  | `mmipal_get_link_packet_counts()` |
| `rx_packets`     | —    | total_increasing  | `mmipal_get_link_packet_counts()` |
| `mcs`            | —    | measurement       | `mmwlan_get_rc_stats()` (delta-based) |
| `bandwidth`      | MHz  | measurement       | `mmwlan_get_rc_stats()` (delta-based) |
| `tx_success_rate` | %   | measurement       | `mmwlan_get_rc_stats()` (delta-based) |

**Text sensors** (`text_sensor.text_sensor_schema()`):
| Config Key         | Source API |
|--------------------|------------|
| `ip_address`       | `mmipal_get_ip_config()` |
| `gateway_address`  | `mmipal_get_ip_config()` |
| `subnet_mask`      | `mmipal_get_ip_config()` |
| `connected_ssid`   | Config value (static) |
| `bssid`            | `mmwlan_get_bssid()` |
| `mac_address`      | `mmwlan_get_mac_addr()` |
| `firmware_version` | `mmwlan_get_version()` |
| `link_quality`     | Derived from MCS + TX success rate |

### Link Quality Sensor
The `link_quality` text sensor provides a human-friendly summary of the radio link:

| Value | MCS | TX Success | Meaning |
|-------|-----|------------|---------|
| Excellent | 5-7 | >95% | Full speed, strong signal |
| Good | 3-4 | >90% | Moderate speed, reliable |
| Fair | 1-2 | >80% | Reduced speed, usable |
| Poor | 0 | >50% | Minimum speed, marginal |
| Critical | any | <50% | Severe packet loss, link at risk |

**MCS (Modulation and Coding Scheme)** is how the radio trades speed for range —
equivalent to LoRa's spreading factor. MCS 7 is fastest (highest throughput, needs
strong signal), MCS 0 is slowest (maximum range, works at weakest signal). The radio
adapts MCS automatically per-frame based on channel conditions.

**TX Success Rate** shows what percentage of frames are delivered without
retransmission. Below 50% means most frames need multiple attempts.

The rate control stats use **delta-based tracking** — comparing `mmwlan_get_rc_stats()`
between 10-second update cycles to find the currently active rate, not the lifetime
most-used rate.

Sensors update every 10 seconds while connected. Static values (MAC, FW version, SSID)
are published once on first connection.

## Power Save and Battery Operation

### Power Save Modes
```c
enum mmwlan_ps_mode { MMWLAN_PS_DISABLED, MMWLAN_PS_ENABLED };
mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);  // Call after mmwlan_boot()
```

- **MMWLAN_PS_ENABLED (default)**: Radio wakes only at each beacon interval (100ms on
  HaLowLink 2) to check for buffered frames. Between beacons, radio sleeps.
  Saves power (~10-30mW avg) but causes missed inbound ARP/ICMP packets, making
  the device intermittently unreachable. **This was the root cause of ping failures.**
- **MMWLAN_PS_DISABLED**: Radio stays awake continuously (~100-200mW). Responds
  instantly. Required for reliable bidirectional communication (API, OTA, ping).

### BUSY Pin Issue (Blocks Power Save / TWT)
The BUSY pin signals when the MM6108 is awake and ready for SPI commands. It's
essential for power save and TWT modes. **It's currently broken in Morse Micro firmware.**

- Morse Micro community confirmed BUSY pin "is no longer happening" in recent firmware
- This is a **firmware bug**, not a Seeed hardware issue — the XIAO HaLow Hat wires BUSY to GPIO 5
- **Workaround**: use `MORSE_CMD_PARAM_ID_WAKE_ACTION_GPIO` to designate a different
  MM6108 GPIO for wake signaling (confirmed working on STM32)
- **Our current approach**: `MMWLAN_PS_DISABLED` bypasses the issue entirely
- Monitor [Morse Micro firmware releases](https://github.com/MorseMicro/firmware_binaries/releases/)
  for a fix

Source: [STM32 Wakeup Issue with MM6108](https://community.morsemicro.com/t/stm32-wakeup-issue-with-mm6108-mf08551-standby-mode/841)

### Target Wake Time (TWT) — 802.11ah's Killer Feature
TWT allows a device to negotiate a sleep schedule with the AP:
- Device tells AP: "Wake me every N seconds for M milliseconds"
- AP buffers packets and delivers them at the agreed time
- Device stays associated but sleeps deeply between TWT windows
- **No re-authentication needed** — skips the 8-10s WPA3-SAE handshake
- Wake time drops to ~100-500ms (vs 10s for full reconnect)
- API: `mmwlan_set_twt_setup()` (available in MM-IoT-SDK)
- HaLowLink 2 supports TWT as AP

### Battery Sensor Architecture
For a battery-powered sensor, the flow would be:

**Without TWT (simple deep sleep):**
1. Wake from ESP32 deep sleep (GPIO/timer/sensor interrupt)
2. Boot ESP32 (~300ms)
3. mmhal_init + mmwlan_boot (~700ms, loads firmware over SPI)
4. mmwlan_sta_enable — full WPA3-SAE handshake (~8-10s)
5. Read sensor, send data (MQTT/API)
6. mmwlan_sta_disable, enter ESP32 deep sleep
7. **Total wake time: ~10-12 seconds per cycle**

**With TWT (stay associated):**
1. Wake from TWT interrupt
2. Send buffered sensor data (~100-500ms)
3. Receive any buffered commands from AP
4. Sleep until next TWT window
5. **Total wake time: ~100-500ms per cycle**

### Power Budget Estimates
- ESP32-S3 deep sleep: ~10μA
- MM6108 held in reset (RESET_N low): ~0μA
- HaLow TX burst: ~300mA for ~30ms
- With TWT at 5-minute interval: ~50-100μA average
- CR123A battery (1500mAh): months to years depending on interval

### AP Idle Timeout
Without TWT, the AP will disassociate the device after its idle timeout
(typically 5-30 minutes of silence, configurable on HaLowLink). After
disassociation, a full 10-second WPA3-SAE reconnect is required.

## HaLow vs LoRa Comparison

| Feature | Wi-Fi HaLow (802.11ah) | LoRa/LoRaWAN |
|---------|----------------------|--------------|
| **Protocol** | Standard IP (TCP/UDP/DHCP) | Proprietary, needs gateway translation |
| **Range** | ~1 km | 10-15 km line-of-sight |
| **Bandwidth** | Up to 32 Mbps | ~50 kbps |
| **Latency** | 7-11 ms (measured) | Minutes (Class A downlink) |
| **Direction** | Full bidirectional | Primarily uplink, limited downlink |
| **Security** | WPA3-SAE | AppKey/NwkKey |
| **OTA updates** | Native (same connection) | Complex, unreliable |
| **Device density** | 1000/AP (managed scheduling) | Degrades with density (ALOHA) |
| **Integration** | Direct IP — ESPHome, MQTT, HA native | Needs gateway + bridge + MQTT translation |
| **Power (TX)** | ~300mA for ~30ms | ~30mA for ~30ms |
| **Reconnect time** | 8-10s (WPA3-SAE) or 100ms (TWT) | ~1s |
| **Module cost** | ~$15-20 (MM6108) | ~$5 (RFM95) |
| **AP/Gateway cost** | ~$100+ (HaLowLink 2) | ~$150+ (LoRa gateway) or free (TTN) |
| **Infrastructure** | Requires HaLow AP | Point-to-point possible with $5 modules |
| **Ecosystem** | New (2025-2026) | Mature (since 2015) |

**Use HaLow when**: You want a real IP network, bidirectional control, OTA updates,
direct Home Assistant integration, and have power for an AP. Dozens of sensors around
a building or campus.

**Use LoRa when**: You need maximum range with minimum infrastructure — a sensor 5km
away, one reading per hour, no need for commands back. Or when every microamp matters
and you can't afford the WPA3 handshake overhead.

## HaLowLink 2 AP Configuration

### Network Mode (Critical)
The HaLowLink 2 Quick Config Wizard controls bridging:
- **"HaLow devices get IP on this device's network"** — Router/NAT mode (default).
  HaLow devices are NATed. Cannot be reached from LAN.
- **"HaLow devices get IP on your existing router's network"** — Bridge mode.
  HaLow devices are L2 bridged to the WAN port. Same subnet as LAN.

Access wizard at: `https://<halowlink-ip>/cgi-bin/luci/admin/morseapwizard`

### Firewall Fix Required
Even in bridge mode, the `wlan` firewall zone has `masq=1` (masquerade/NAT) enabled
by default. This hides HaLow devices behind the HaLowLink's IP. **Must disable:**
```bash
ssh root@<halowlink-ip>  # password from device label
uci set firewall.wlan.masq=0
uci commit firewall
/etc/init.d/firewall restart
```

### Port Wiring
- **WAN port**: Connect to your LAN. In bridge mode, the wizard bridges WAN↔HaLow.
- **LAN port**: Used for management access (separate bridge, not for HaLow traffic).
- Plugging into the LAN port instead of WAN will get DHCP but no L2 bridging.

### Bridge Topology
```
br-wlan: wlan0 (HaLow radio) + wan (WAN ethernet port)  ← bridged for data
br-lan:  lan (LAN port) + phy0-ap0 (2.4GHz WiFi) + usblan  ← management
```

### LWIP netif_set_link_up Fix
On ESP-IDF 5.5.2 with the Seeed SDK, the mmipal link-up callback doesn't fire
reliably. Without `netif_set_link_up()`, LWIP won't respond to ARP even though
the radio is connected. The component forces this after IP acquisition by finding
the netif via MAC match in `netif_list`. Must use `LOCK_TCPIP_CORE()` wrapper.

### OTA Shutdown Handler (Critical for Reliable OTA)
After `esp_restart()` (OTA reboot), the ESP32's SPI2 peripheral and the MM-IoT-SDK's
internal state are corrupted. Simply resetting the SPI hardware on boot is insufficient.

**Fix**: Register `esp_register_shutdown_handler()` that runs BEFORE `esp_restart()`:

```c
static void halow_shutdown_handler() {
    mmwlan_sta_disable();              // Disconnect STA
    mmwlan_shutdown();                 // Power down MM6108
    mmwlan_deinit();                   // Clean up SDK + SPI bus
    periph_module_reset(PERIPH_SPI2_MODULE);  // Reset SPI hardware
    gpio_set_level(RESET_N, 0);        // Hold MM6108 in reset
}

// In setup():
esp_register_shutdown_handler(halow_shutdown_handler);
```

This properly deinitializes the WLAN stack and SPI bus while still running, so the
next boot starts with a clean slate. Verified with 3 consecutive OTA cycles, all clean.

### OTA Updates
- Upload: 1.05 MB in 8-13 seconds (~112 KB/s) over HaLow link
- Reboot after OTA: reliable (shutdown handler cleans up before restart)
- Device reconnects to AP and HA automatically after OTA reboot
- Verified: 3 consecutive OTA uploads, zero crash loops

### Firmware Release Monitoring
Watch these repos for SDK updates (BUSY pin fix, IDF 5.5 native support):
- [MorseMicro/mm-iot-esp32](https://github.com/MorseMicro/mm-iot-esp32) — ESP32 SDK
- [MorseMicro/firmware_binaries](https://github.com/MorseMicro/firmware_binaries/releases/) — Firmware releases
- [Morse Micro Community](https://community.morsemicro.com) — BUSY pin fix discussion

## Verified Test Results

| Date | Test | Result |
|------|------|--------|
| 2026-04-30 | SPI probe (raw SDIO CMD0/CMD5/CMD52) | CCCR registers readable, FBR1 accessible |
| 2026-04-30 | Network scan (Seeed SDK, IDF 5.1.1) | Found halowlink2-627b, RSSI -41 dBm, 8 MHz, SAE |
| 2026-04-30 | WPA3-SAE connection (Seeed SDK, IDF 5.1.1) | Link up in ~8s |
| 2026-04-30 | DHCP over HaLow | IP 192.168.12.164, GW 192.168.12.1, mask /24 |
| 2026-04-30 | LWIP + iperf UDP server | Server listening on port 5001, IPv4 + IPv6 |
| 2026-04-30 | ESPHome compile (Seeed SDK, IDF 5.5.2) | 1.08MB firmware, 59% flash |
| 2026-04-30 | ESPHome full boot with sensors | All sensors reporting, API+OTA running |
| 2026-04-30 | mDNS registration | `halow-test.local` registered on HaLow interface |
| 2026-04-30 | Upstream SDK (v2.10.4, IDF 5.5.2) | FW loads but connection fails (BCF issue) |
| 2026-04-30 | 20-min soak test | Heap stable at 304,520 (zero delta), 0 errors |
| 2026-05-01 | Ping from LAN (power save ON) | Intermittent: 5/20 success, ARP timeouts |
| 2026-05-01 | Ping from LAN (power save OFF) | **10/10, 0% loss, 7-11ms latency** |
| 2026-05-01 | ESPHome API port from LAN | TCP connect to 192.168.1.86:6053 succeeded |
| 2026-05-01 | Home Assistant connected | HA 2026.4.2 connected, all entities visible |
| 2026-05-01 | Auto-download SDK | Clean build from scratch succeeds in ~140s |
| 2026-05-01 | AP power cycle reconnect | **Working** — LINK LOST → RECOVERED after 53s, no crash |
| 2026-05-02 | Range walk reconnect | **Working** — 16s recovery via FreeRTOS timer, no crash |
| 2026-05-02 | Range test RSSI profile | -29 (close) → -70 (edge) → STA DISABLED → reconnect at -60 |
| 2026-05-02 | Antenna disconnect test | **Working** — 85s recovery (antenna off ~70s), 3 attempts, no crash |
| 2026-05-02 | Repeated antenna cycles | **Working** — 2 cycles, 113s + 154s recovery, no crash |
| 2026-05-02 | MCS adaptation observed | MCS 7→0→1→2→...→7 during antenna loosen/tighten |
| 2026-05-02 | OTA over HaLow | **Working** — 1.05MB in 8-13s, reboot + reconnect clean |
| 2026-05-02 | OTA reboot recovery | **Fixed** — periph_module_reset(SPI2) prevents crash loop |

### Working Configuration (Seeed SDK + IDF 5.5.2)
```
Morse firmware version 1.13.1
morselib version 2.6.4-esp32
Morse chip ID 0x306
Actual SPI CLK 40000kHz
HaLow MAC: A8:DD:9F:4D:C6:01
Setup time: ~720ms (to mmwlan_sta_enable)
Connection time: ~10s from cold boot
RSSI: -33 to -37 dBm
Ping latency: 7-11ms (power save disabled)
DHCP IP: 192.168.1.86 (bridged via HaLowLink 2 WAN port)
Gateway: 192.168.1.1
BSSID: 50:2E:91:D2:C9:E4
BCF: bcf_mf16858_us.mbin
mDNS: halow-test.local registered
ESPHome API: port 6053 (reachable from LAN)
ESPHome OTA: port 3232
Power save: disabled (required for reliable inbound connectivity)
Heap: 304,520 bytes free (stable, no leaks)
HA: Home Assistant 2026.4.2 connected via API
SDK: auto-downloaded from sweitzja/mm-iot-esp32 fork
Network: --wrap on is_connected() for API/OTA provider registration
Reconnect: RSSI + STA state monitoring, auto-retry with 5s backoff
```

## Development Environment

### Local Paths
- Project: `/home/jason/esphome-halow/`
- ESP-IDF (standalone): `~/esp/esp-idf-v5.1.1/`
- MM-IoT-SDK (Seeed, patched): `~/esp/mm-iot-esp32/`
- MM-IoT-SDK (upstream): `~/esp/mm-iot-esp32-upstream/`
- PlatformIO toolchain: `~/.platformio/packages/toolchain-xtensa-esp-elf/`

### Serial Access (WSL2)
1. In Windows PowerShell (admin): `usbipd attach --wsl --busid 5-2`
2. In WSL: `sudo chmod 666 /dev/ttyACM0` (or add user to `dialout` group)
3. Device appears as `/dev/ttyACM0` (USB JTAG/serial debug)

## Related Projects
- [MorseMicro mm-iot-esp32](https://github.com/MorseMicro/mm-iot-esp32) -- Upstream SDK (v2.10.4, IDF >=5.1.1)
- [Seeed mm-iot-esp32](https://github.com/Seeed-Studio/mm-iot-esp32) -- Seeed fork (v2.6.4, **used with patches**)
- [MorseMicro morse-firmware](https://github.com/MorseMicro/morse-firmware) -- BCF files for Quectel modules (.bin format, needs conversion)
- [Xiao-Halow-to-WiFi-Bridge](https://github.com/gtgreenw/Xiao-Halow-to-WiFi-Bridge) -- Community ESP32-S3 HaLow bridge
- [Morse Micro Community](https://community.morsemicro.com) -- Forum for BCF and SDK questions

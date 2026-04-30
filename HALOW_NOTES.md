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

### Binary Files (from MM-IoT-SDK `framework/morsefirmware/`)
| File                    | Purpose                           | Notes |
|-------------------------|-----------------------------------|-------|
| `mm6108.mbin`           | MM6108 SoC firmware (v1.13.1)     | Loaded over SPI at boot |
| `bcf_mf16858_us.mbin`   | Board config: FGH100M-H, US reg   | Default for XIAO HaLow Hat |
| `bcf_mf08651_us.mbin`   | Board config: alternate module     | For different module variants |
| `bcf_mf08551.mbin`      | Board config: non-US variant       | |
| `bcf_mf08251.mbin`      | Board config: another variant      | |

### How Firmware Loading Works
1. ESP32 holds MM6108 in reset (RESET_N low)
2. ESP32 releases reset, asserts WAKE
3. ESP32 initializes SPI bus at 400 kHz, sends init clocks
4. SDIO-over-SPI initialization (CMD0, CMD5, CMD52)
5. MM-IoT-SDK loads `mm6108.mbin` via multi-byte SPI writes
6. MM-IoT-SDK loads BCF via multi-byte SPI writes
7. MM6108 boots its internal firmware, SPI clock increases to 40 MHz
8. Total boot time: ~500ms

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
│   │   ├── mmhal_core.c   # SPI bus init, firmware loading
│   │   ├── mmhal_wlan.c   # WLAN HAL bindings
│   │   └── mmhal_wlan_binaries.c  # Embeds .mbin files as C arrays
│   ├── src/
│   │   ├── mmipal/        # IP abstraction (LWIP netif, DHCP)
│   │   ├── mmiperf/       # iperf implementation
│   │   ├── mmpktmem/      # Packet memory manager
│   │   └── mmutils/       # OS abstraction, config store
│   └── morsefirmware/     # .mbin firmware files
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
- `mmwlan_regdb.def` -- Regulatory database (channel lists per country code)

### ESP-IDF Component Dependencies
When building as an ESP-IDF component, the SDK requires:
- `driver` (SPI, GPIO)
- `freertos`
- `lwip`
- `mbedtls` (for crypto, though libmorse_nocrypto.a doesn't use it directly)
- `nvs_flash` (optional, for config store)

### Required sdkconfig Settings
```
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_TIMER_TASK_PRIORITY=10    # Higher than MMOSAL_TASK_PRI_HIGH
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
CONFIG_MBEDTLS_NIST_KW_C=y
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
- `mmipal_get_link_state()` -> LINK_UP or LINK_DOWN
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
mmwlan_sta_disable();   // Brings link down, returns MMWLAN_SUCCESS
// Then re-call:
mmwlan_sta_enable(&sta_args, sta_status_cb);  // Starts new connection
```

## mDNS Known Limitation

mmipal creates a raw LWIP netif (`struct netif`), NOT an `esp_netif_t`. ESP-IDF's
espressif/mdns component requires `esp_netif_t*` for `mdns_register_netif()`.
The LWIP netif is static/private inside mmipal — no public getter.

**Current workaround**: mDNS is disabled. Device is reachable by IP address only.
CONFIG_MDNS_PREDEF_NETIF_STA and CONFIG_ESP_WIFI_ENABLED are set to False to prevent
the mDNS component from searching for WiFi interfaces that don't exist.

**Future fix options**:
1. Add `mmipal_get_netif()` upstream and create a minimal `esp_netif_t` wrapper
2. Use LWIP's built-in mDNS responder (`lwip/apps/mdns.h`) directly
3. Wait for Morse Micro to add esp_netif integration to their SDK

## ESPHome Integration Architecture

### Component Design
```
components/mm_halow/
├── __init__.py              # CONFIG_SCHEMA, to_code(), build flags
├── mm_halow_component.h     # Class declaration with sensor pointers
├── mm_halow_component.cpp   # MM-IoT-SDK bridge with state machine
└── pre_build.py             # PlatformIO script: generates firmware .o files
```

### State Machine
```
STOPPED ─── setup() ok ──► CONNECTING ─── link up + IP ──► CONNECTED
                               ▲  timeout 30s: retry          │
                               └───────── link drops ──────────┘
```

### Build System (solved)
- `add_idf_component(path=...)` for morselib, mm_shims, mmipal, mmutils, mmpktmem, mmregdb
- `add_idf_sdkconfig_option()` for Kconfig (pins, FreeRTOS, BCF, chip type)
- `pre_build.py` runs ninja to generate objcopy .o files for firmware blobs
- `env.Append(LINKFLAGS=...)` adds .o files to linker command
- Uses upstream MorseMicro SDK (supports ESP-IDF >=5.1.1, tested with 5.5.2)

## Verified Test Results

| Date | Test | Result |
|------|------|--------|
| 2026-04-30 | SPI probe (raw SDIO CMD0/CMD5/CMD52) | CCCR registers readable, FBR1 accessible |
| 2026-04-30 | Network scan | Found halowlink2-627b, RSSI -41 dBm, 8 MHz, SAE |
| 2026-04-30 | WPA3-SAE connection | Link up in ~8s, STA state transitions clean |
| 2026-04-30 | DHCP over HaLow | IP 192.168.12.164, GW 192.168.12.1, mask /24 |
| 2026-04-30 | LWIP + iperf UDP server | Server listening on port 5001, IPv4 + IPv6 |

| 2026-04-30 | ESPHome compile (IDF 5.5.2) | 1.08MB firmware, 59% flash |
| 2026-04-30 | ESPHome full boot | API (6053) + OTA (3232) over HaLow, DHCP IP |

### Firmware Details (upstream SDK)
```
Morse firmware version 1.17.6
morselib version 2.10.4-esp32
Morse chip ID 0x306
Actual SPI CLK 40000kHz
HaLow MAC: a8:dd:9f:4d:c6:01
Setup time: 770ms (to mmwlan_sta_enable)
Link-up time: ~10s from cold boot
```

## Development Environment

### Local Paths
- Project: `/home/jason/esphome-halow/`
- ESP-IDF (standalone): `~/esp/esp-idf-v5.1.1/`
- MM-IoT-SDK (Seeed): `~/esp/mm-iot-esp32/`
- MM-IoT-SDK (upstream): `~/esp/mm-iot-esp32-upstream/`
- PlatformIO toolchain: `~/.platformio/packages/toolchain-xtensa-esp-elf/`

### Serial Access (WSL2)
1. In Windows PowerShell (admin): `usbipd attach --wsl --busid 5-2`
2. In WSL: `sudo chmod 666 /dev/ttyACM0` (or add user to `dialout` group)
3. Device appears as `/dev/ttyACM0` (USB JTAG/serial debug)

## Related Projects
- [MorseMicro mm-iot-esp32](https://github.com/MorseMicro/mm-iot-esp32) -- Primary SDK (upstream, IDF >=5.1.1)
- [Seeed mm-iot-esp32](https://github.com/Seeed-Studio/mm-iot-esp32) -- Seeed fork (IDF 5.1.1 only)
- [Xiao-Halow-to-WiFi-Bridge](https://github.com/gtgreenw/Xiao-Halow-to-WiFi-Bridge) -- ESP32-S3 HaLow bridge with NAT, web config, RSSI monitoring
- [MorseMicro morse-firmware](https://github.com/MorseMicro/morse-firmware) -- Additional BCF files for Quectel modules

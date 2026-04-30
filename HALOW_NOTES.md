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
| `mm6108.mbin` (Seeed)   | v2.6.4-esp32 | 1.13.1 | 2.6.4-esp32  | **Working** |
| `mm6108.mbin` (upstream) | v2.10.4-esp32 | 1.17.6 | 2.10.4-esp32 | Loads but connection fails |

**Critical**: Firmware and BCF must be from the same SDK version. Mixing Seeed BCF with
upstream firmware (or vice versa) causes `FW manifest pointer not set` crash.

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

## SDK Compatibility and Patches

### Why Seeed SDK (v2.6.4), not upstream (v2.10.4)

The upstream MM-IoT-SDK v2.10.4 compiles and boots the MM6108 firmware on ESP-IDF 5.5.2,
but the `bcf_mf16858.mbin` BCF it ships does not have US regulatory settings. The radio
cannot scan/connect on 902-928 MHz. The Seeed fork v2.6.4 ships `bcf_mf16858_us.mbin`
which works correctly.

### Required Patches to Seeed SDK for ESP-IDF 5.5.2

The Seeed fork is pinned to ESP-IDF 5.1.1 but works on 5.5.2 with two patches:

**Patch 1: Relax IDF version constraints**

All `idf_component.yml` files under `framework/` contain `version: "==5.1.1"`.
Change to `version: ">=5.1.1"`:

```bash
find ~/esp/mm-iot-esp32/framework -name "idf_component.yml" \
  -exec sed -i 's/version: "==5.1.1"/version: ">=5.1.1"/' {} \;
```

Files affected (6 total):
- `framework/morselib/idf_component.yml`
- `framework/mm_shims/idf_component.yml`
- `framework/src/mmipal/idf_component.yml`
- `framework/src/mmutils/idf_component.yml`
- `framework/src/mmpktmem/idf_component.yml`
- `framework/src/mmiperf/idf_component.yml`

**Patch 2: Fix `ESP_SYSTEM_INIT_FN` macro for IDF 5.3+**

ESP-IDF 5.3 added a `stage_` parameter to the `ESP_SYSTEM_INIT_FN` macro.
In `framework/mm_shims/mmosal_shim_freertos_esp32.c`, line 127:

```diff
-ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, BIT(0), 999)
+ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, SECONDARY, BIT(0), 999)
```

```bash
sed -i 's/ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, BIT(0), 999)/ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, SECONDARY, BIT(0), 999)/' \
  ~/esp/mm-iot-esp32/framework/mm_shims/mmosal_shim_freertos_esp32.c
```

### Other SDK Differences

| Feature | Seeed v2.6.4 | Upstream v2.10.4 |
|---------|-------------|-----------------|
| Regulatory DB | `mmwlan_regdb.def` (inline header) | `mmregdb` (separate IDF component) |
| BCF format | Pre-built `.o` objects in mm_shims/ | Runtime objcopy from `.mbin` files |
| BCF config | Kconfig choice (`CONFIG_MM_BCF_MF16858_US`) | Kconfig string (`CONFIG_MM_BCF_FILE`) |
| IDF compat | `==5.1.1` (needs patch for 5.5.2) | `>=5.1.1` (native) |
| FW binary | Pre-built `mm6108.mbin.o` in mm_shims/ | Runtime objcopy from mm6108.mbin |
| mm_shims source | `mmhal.c`, `wlan_hal.c` | `mmhal_core.c`, `mmhal_os.c`, `mmhal_wlan.c` |
| `driver` dep | Uses legacy `driver` component | Detects `esp_driver_*` split for IDF 5.3+ |

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

## mDNS Known Limitation

mmipal creates a raw LWIP netif (`struct netif`), NOT an `esp_netif_t`. ESP-IDF's
espressif/mdns component requires `esp_netif_t*` for `mdns_register_netif()`.
The LWIP netif is static/private inside mmipal — no public getter.

**Current workaround**: mDNS is not functional. Device is reachable by IP address only.
The mDNS component initializes but reports `ESP_ERR_INVALID_STATE` because it cannot
find a registered network interface.

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
└── pre_build.py             # PlatformIO script: firmware blob linking
```

### State Machine
```
setup() ok ──► CONNECTING ─── link up + IP ──► CONNECTED
                   ▲  timeout 60s: disable+retry    │
                   │                                 │
               STOPPED ◄──── link drops ─────────────┘
                   │  wait 3s
                   └──► CONNECTING (auto-reconnect)
```

### Build System
- `add_idf_component(path=...)` for morselib, mm_shims, mmipal, mmutils, mmpktmem
  (and mmregdb if present — upstream SDK only)
- `add_idf_sdkconfig_option()` for Kconfig (pins, FreeRTOS, BCF, chip type)
- `pre_build.py` handles firmware blob linking:
  - **Upstream SDK**: runs ninja to invoke CMake custom targets (objcopy .mbin -> .o)
  - **Seeed SDK**: copies pre-built .mbin.o files from mm_shims/ directory
  - Auto-detects BCF name from Kconfig choice (Seeed) or string (upstream)
  - Always adds .o paths to `LINKFLAGS` for PlatformIO's SCons linker
- Uses Seeed MM-IoT-SDK v2.6.4 (with patches for IDF 5.5.2 compatibility)

### Sensors
All sensors are optional YAML config entries within the `mm_halow:` block:

**Numeric sensors** (`sensor.sensor_schema()`):
| Config Key    | Unit | Device Class      | Source API |
|---------------|------|-------------------|------------|
| `rssi`        | dBm  | signal_strength   | `mmwlan_get_rssi()` |
| `tx_packets`  | —    | total_increasing  | `mmipal_get_link_packet_counts()` |
| `rx_packets`  | —    | total_increasing  | `mmipal_get_link_packet_counts()` |

**Text sensors** (`text_sensor.text_sensor_schema()`):
| Config Key         | Source API |
|--------------------|------------|
| `ip_address`       | `mmipal_get_ip_config()` |
| `gateway_address`  | `mmipal_get_ip_config()` |
| `subnet_mask`      | `mmipal_get_ip_config()` |
| `connected_ssid`   | Config value (static) |
| `bssid`            | `mmwlan_get_bssid()` |
| `mac_address`      | `mmwlan_get_mac_addr()` |
| `firmware_version`  | `mmwlan_get_version()` |

Sensors update every 10 seconds while connected. Static values (MAC, FW version, SSID)
are published once on first connection.

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
| 2026-04-30 | Upstream SDK (v2.10.4, IDF 5.5.2) | FW loads but connection fails (BCF issue) |

### Working Configuration (Seeed SDK + IDF 5.5.2)
```
Morse firmware version 1.13.1
morselib version 2.6.4-esp32
Morse chip ID 0x306
Actual SPI CLK 40000kHz
HaLow MAC: A8:DD:9F:4D:C6:01
Setup time: 718ms (to mmwlan_sta_enable)
Connection time: ~10s from cold boot
RSSI: -37 dBm
DHCP IP: 192.168.12.164
Gateway: 192.168.12.1
BSSID: 50:2E:91:D2:C9:E4
BCF: bcf_mf16858_us.mbin
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

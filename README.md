# esphome-halow

ESPHome external component for **Wi-Fi HaLow** (IEEE 802.11ah) using the [Morse Micro MM6108](https://www.morsemicro.com/chips/) SoC. Wi-Fi HaLow operates in sub-1 GHz spectrum (902-928 MHz in North America), offering up to 1 km range with low power consumption -- ideal for IoT devices that need to reach beyond standard Wi-Fi range.

This component provides a drop-in network interface for ESPHome, replacing `wifi:` with `mm_halow:`. It works like the built-in W5500/Ethernet SPI support: configure your pins, SSID, and password in YAML, and get full Home Assistant connectivity over HaLow.

## Supported Hardware

### HaLow Modules (MM6108-based)
| Module | Form Factor | Notes |
|--------|------------|-------|
| [Seeed XIAO HaLow Hat](https://www.seeedstudio.com/Wio-WM6180-Wi-Fi-HaLow-Module-for-XIAO-p-6395.html) (WIO-WM6180) | XIAO hat | Plug-and-play with XIAO ESP32-S3 |
| Quectel FGH100M-H | Module | Same MM6108 SoC, different form factor |

### Host MCUs
| Board | Status | Notes |
|-------|--------|-------|
| Seeed XIAO ESP32-S3 | Tested | Default pin mapping matches HaLow Hat |
| Other ESP32-S3 boards | Should work | Configure SPI pins in YAML |
| ESP32-C6 | Untested | Supported by MM-IoT-SDK |

### Access Points
| AP | Status | Notes |
|----|--------|-------|
| [GL-iNet HaLowLink 2](https://www.gl-inet.com/products/halowlink2/) | Tested | MM8108-based, WPA3-SAE, bridges to 2.4 GHz |
| Morse Micro reference APs | Should work | Any 802.11ah AP |

## Quick Start

### Prerequisites
- ESPHome 2024.2+ with ESP-IDF framework support
- [Seeed MM-IoT-SDK](https://github.com/Seeed-Studio/mm-iot-esp32) (cloned locally, with patches)

Clone and patch the SDK:
```bash
git clone https://github.com/Seeed-Studio/mm-iot-esp32.git ~/esp/mm-iot-esp32

# Patch 1: Relax IDF version constraints (required for ESPHome's IDF 5.5.2)
find ~/esp/mm-iot-esp32/framework -name "idf_component.yml" \
  -exec sed -i 's/version: "==5.1.1"/version: ">=5.1.1"/' {} \;

# Patch 2: Fix ESP_SYSTEM_INIT_FN macro for IDF 5.3+
sed -i 's/ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, BIT(0), 999)/ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, SECONDARY, BIT(0), 999)/' \
  ~/esp/mm-iot-esp32/framework/mm_shims/mmosal_shim_freertos_esp32.c
```

### Example YAML

```yaml
esphome:
  name: halow-sensor
  friendly_name: HaLow Sensor

esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: esp-idf
    version: recommended

external_components:
  - source: github://sweitzja/esphome-halow
    components: [mm_halow]

mm_halow:
  ssid: "my-halow-ap"
  password: "my-password"
  country_code: "US"
  security: SAE                        # SAE (WPA3), OWE, or OPEN
  bcf_file: "bcf_mf16858_us.mbin"     # Board config for FGH100M-H (US)
  mm_iot_sdk_path: "~/esp/mm-iot-esp32"
  # manual_ip:                         # Optional static IP
  #   static_ip: 192.168.12.100
  #   gateway: 192.168.12.1
  #   subnet: 255.255.255.0
  # Pins default to XIAO HaLow Hat. Override for other boards:
  # clk_pin: 7  / mosi_pin: 9 / miso_pin: 8 / cs_pin: 4
  # irq_pin: 3 / reset_pin: 1 / wake_pin: 2 / busy_pin: 5

  # Optional diagnostic sensors
  rssi:
    name: "HaLow RSSI"
  ip_address:
    name: "HaLow IP"
  mac_address:
    name: "HaLow MAC"

api:

logger:

ota:
  platform: esphome

sensor:
  - platform: uptime
    name: Uptime
```

## How It Works

### Architecture

The MM6108 is a dedicated Wi-Fi HaLow SoC that communicates with the ESP32 host over SPI using an SDIO-over-SPI protocol. Unlike a simple peripheral, the MM6108 runs its own 802.11ah MAC/PHY stack and requires firmware loaded at every boot.

```
                    SPI (40 MHz)
ESP32-S3 ◄────────────────────────► MM6108 (HaLow SoC)
  │                                    │
  ├─ ESPHome                           ├─ 802.11ah MAC/PHY
  ├─ LWIP (via mmipal)                 ├─ WPA3-SAE
  ├─ Home Assistant API                ├─ Sub-1 GHz radio
  └─ OTA updates                       └─ Up to 1 km range
```

### Boot Sequence

1. **mmhal_init()** -- Initialize SPI bus, GPIO pins (reset, wake, busy, IRQ)
2. **mmwlan_init()** -- Initialize WLAN subsystem
3. **mmwlan_boot()** -- Load `mm6108.mbin` firmware + `bcf_mf16858.mbin` board config over SPI
4. **mmipal_init()** -- Initialize LWIP network stack with DHCP
5. **mmwlan_sta_enable()** -- Start WPA3-SAE association with the AP
6. Link comes up, DHCP assigns IP address. Full setup takes ~10s from cold boot.

### Pin Mapping

Default pin mapping for the Seeed XIAO HaLow Hat:

| Function | GPIO | XIAO Pin | Direction |
|----------|------|----------|-----------|
| SPI SCK | 7 | D8 | Output |
| SPI MISO | 8 | D9 | Input |
| SPI MOSI | 9 | D10 | Output |
| SPI CS | 4 | D4 | Output |
| SPI IRQ | 3 | D3 | Input (data-ready interrupt) |
| RESET_N | 1 | D1 | Output (active low) |
| WAKE | 2 | D2 | Output |
| BUSY | 5 | D5 | Input (rising edge) |

Source: [Seeed mm-iot-esp32 Kconfig](https://github.com/Seeed-Studio/mm-iot-esp32/blob/main/framework/mm_shims/Kconfig)

### Firmware Files

The MM6108 has no persistent firmware. Two binary files are embedded in the ESP32 flash and loaded over SPI at every boot:

| File | Purpose | Size | Source |
|------|---------|------|--------|
| `mm6108.mbin` | MM6108 SoC firmware (v1.13.1) | ~400 KB | Seeed MM-IoT-SDK `framework/morsefirmware/` |
| `bcf_mf16858_us.mbin` | Board config for FGH100M-H (US regulatory) | ~344 B | Seeed MM-IoT-SDK `framework/morsefirmware/` |

**Important**: Firmware and BCF must be from the same SDK version. The Seeed SDK v2.6.4
ships matching files that work together. See [HALOW_NOTES.md](HALOW_NOTES.md) for BCF
compatibility details and other available BCF files.

## Verified Test Results

All testing performed with XIAO ESP32-S3 + XIAO HaLow Hat + GL-iNet HaLowLink 2:

| Test | Result |
|------|--------|
| SPI communication (SDIO registers) | Pass -- CCCR and FBR registers readable |
| MM6108 firmware boot | Pass -- FW v1.13.1, morselib v2.6.4-esp32, chip ID 0x306 |
| HaLow network scan | Pass -- Found AP at -41 dBm, 8 MHz BW |
| WPA3-SAE authentication | Pass -- Link up in ~10 seconds |
| DHCP IP acquisition | Pass -- 192.168.12.164 from gateway 192.168.12.1 |
| LWIP TCP/UDP stack | Pass -- iperf server operational |
| ESPHome compile (IDF 5.5.2) | Pass -- 1.08MB firmware, 59% flash usage |
| **ESPHome full boot with sensors** | **Pass** -- RSSI (-35 dBm), IP, MAC, BSSID, FW version all reporting |
| **mDNS discovery** | **Pass** -- `halow-test.local` registered on HaLow interface |

## Component Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ssid` | string | **required** | HaLow AP SSID |
| `password` | string | **required** | WPA3-SAE passphrase |
| `security` | enum | `SAE` | Security type: `SAE` (WPA3), `OWE`, or `OPEN` |
| `bcf_file` | string | `bcf_mf16858_us.mbin` | Board configuration file |
| `manual_ip` | schema | *(DHCP)* | Static IP config (static_ip, gateway, subnet, dns1, dns2) |
| `country_code` | string | `"US"` | Regulatory domain (US, AU, EU, JP, etc.) |
| `clk_pin` | int | `7` | SPI clock GPIO |
| `mosi_pin` | int | `9` | SPI MOSI GPIO |
| `miso_pin` | int | `8` | SPI MISO GPIO |
| `cs_pin` | int | `4` | SPI chip select GPIO |
| `irq_pin` | int | `3` | SPI interrupt GPIO |
| `reset_pin` | int | `1` | Module reset GPIO (active low) |
| `wake_pin` | int | `2` | Module wake GPIO |
| `busy_pin` | int | `5` | Module busy GPIO |

## Dependencies

This component depends on the [Morse Micro MM-IoT-SDK](https://github.com/Seeed-Studio/mm-iot-esp32) which provides:

- **morselib** -- Prebuilt binary library containing the WLAN driver and crypto
- **mm_shims** -- ESP-IDF HAL layer (SPI, GPIO, FreeRTOS integration)
- **mmipal** -- IP abstraction layer (LWIP netif binding)
- **mmutils** -- Utility functions (OS abstraction, config store)
- **morsefirmware/** -- MM6108 firmware and board config binaries

The SDK is licensed under Apache-2.0 (shims/examples) and a Morse Micro BDL (binary libraries).

## References

- [Seeed MM-IoT-SDK](https://github.com/Seeed-Studio/mm-iot-esp32) -- **Used by this component** (v2.6.4, with patches for IDF 5.5.2)
- [Morse Micro MM-IoT-SDK](https://github.com/MorseMicro/mm-iot-esp32) -- Upstream (v2.10.4, IDF >=5.1.1, but US BCF missing)
- [Morse Micro morse-firmware](https://github.com/MorseMicro/morse-firmware) -- Additional BCF files for Quectel modules
- [Morse Micro MM6108 Datasheet](https://www.morsemicro.com/chips/) -- SoC specifications
- [Seeed Wiki: Getting Started with Wi-Fi HaLow](https://wiki.seeedstudio.com/getting_started_with_wifi_halow_module_for_xiao/) -- Hardware setup guide
- [IEEE 802.11ah Standard](https://en.wikipedia.org/wiki/IEEE_802.11ah) -- Wi-Fi HaLow specification
- [Xiao-Halow-to-WiFi-Bridge](https://github.com/gtgreenw/Xiao-Halow-to-WiFi-Bridge) -- Related community project

## License

Apache-2.0. Morse Micro binary libraries are subject to the [Morse Micro BDL](https://github.com/Seeed-Studio/mm-iot-esp32/blob/main/LICENSES/LicenseRef-MorseMicroBDL.txt).

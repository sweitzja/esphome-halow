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
- [Morse Micro MM-IoT-SDK](https://github.com/MorseMicro/mm-iot-esp32) (upstream, cloned locally)

Clone the SDK:
```bash
git clone https://github.com/MorseMicro/mm-iot-esp32.git ~/esp/mm-iot-esp32
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
  mm_iot_sdk_path: "~/esp/mm-iot-esp32"
  # Pins default to XIAO HaLow Hat mapping. Override for other boards:
  # clk_pin: 7
  # mosi_pin: 9
  # miso_pin: 8
  # cs_pin: 4
  # irq_pin: 3
  # reset_pin: 1
  # wake_pin: 2
  # busy_pin: 5

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
| `mm6108.mbin` | MM6108 SoC firmware (v1.17.6) | ~400 KB | MM-IoT-SDK `framework/morsefirmware/` |
| `bcf_mf16858.mbin` | Board config for FGH100M-H (US regulatory) | ~2 KB | MM-IoT-SDK `framework/morsefirmware/mm6108/bcfs/` |

Other BCF files are available for different modules and regulatory domains.

## Verified Test Results

All testing performed with XIAO ESP32-S3 + XIAO HaLow Hat + GL-iNet HaLowLink 2:

| Test | Result |
|------|--------|
| SPI communication (SDIO registers) | Pass -- CCCR and FBR registers readable |
| MM6108 firmware boot | Pass -- FW v1.17.6, morselib v2.10.4-esp32, chip ID 0x306 |
| HaLow network scan | Pass -- Found AP at -41 dBm, 8 MHz BW |
| WPA3-SAE authentication | Pass -- Link up in ~10 seconds |
| DHCP IP acquisition | Pass -- 192.168.12.164 from gateway 192.168.12.1 |
| LWIP TCP/UDP stack | Pass -- iperf server operational |
| ESPHome compile | Pass -- 1.08MB firmware, 59% flash usage |
| **ESPHome full boot** | **Pass** -- API server + OTA over HaLow, DHCP IP acquired |

## Component Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ssid` | string | **required** | HaLow AP SSID |
| `password` | string | **required** | WPA3-SAE passphrase |
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

- [Morse Micro MM-IoT-SDK](https://github.com/MorseMicro/mm-iot-esp32) -- Primary SDK (upstream, supports ESP-IDF >=5.1.1)
- [Seeed MM-IoT-SDK fork](https://github.com/Seeed-Studio/mm-iot-esp32) -- Seeed's fork (pinned to ESP-IDF 5.1.1)
- [Morse Micro MM6108 Datasheet](https://www.morsemicro.com/chips/) -- SoC specifications
- [Seeed Wiki: Getting Started with Wi-Fi HaLow](https://wiki.seeedstudio.com/getting_started_with_wifi_halow_module_for_xiao/) -- Hardware setup guide
- [IEEE 802.11ah Standard](https://en.wikipedia.org/wiki/IEEE_802.11ah) -- Wi-Fi HaLow specification
- [Xiao-Halow-to-WiFi-Bridge](https://github.com/gtgreenw/Xiao-Halow-to-WiFi-Bridge) -- Related community project

## License

Apache-2.0. Morse Micro binary libraries are subject to the [Morse Micro BDL](https://github.com/Seeed-Studio/mm-iot-esp32/blob/main/LICENSES/LicenseRef-MorseMicroBDL.txt).

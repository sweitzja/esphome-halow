"""ESPHome component for Wi-Fi HaLow (IEEE 802.11ah) via Morse Micro MM6108."""

import os
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_PASSWORD,
    CONF_SSID,
)
from esphome.core import coroutine_with_priority, CORE
from esphome.components.esp32 import add_idf_sdkconfig_option, add_idf_component

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network"]
CONFLICTS_WITH = ["wifi"]

CONF_COUNTRY_CODE = "country_code"
CONF_CLK_PIN = "clk_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_MISO_PIN = "miso_pin"
CONF_CS_PIN = "cs_pin"
CONF_IRQ_PIN = "irq_pin"
CONF_RESET_PIN = "reset_pin"
CONF_WAKE_PIN = "wake_pin"
CONF_BUSY_PIN = "busy_pin"
CONF_MM_IOT_SDK_PATH = "mm_iot_sdk_path"

mm_halow_ns = cg.esphome_ns.namespace("mm_halow")
MMHalowComponent = mm_halow_ns.class_("MMHalowComponent", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MMHalowComponent),
            cv.Required(CONF_SSID): cv.string,
            cv.Required(CONF_PASSWORD): cv.string,
            cv.Optional(CONF_COUNTRY_CODE, default="US"): cv.string_strict,
            # Path to the MM-IoT-SDK clone (Seeed fork)
            cv.Optional(
                CONF_MM_IOT_SDK_PATH,
                default="~/esp/mm-iot-esp32",
            ): cv.string,
            # SPI pins - defaults match XIAO HaLow Hat
            cv.Optional(CONF_CLK_PIN, default=7): cv.int_,
            cv.Optional(CONF_MOSI_PIN, default=9): cv.int_,
            cv.Optional(CONF_MISO_PIN, default=8): cv.int_,
            cv.Optional(CONF_CS_PIN, default=4): cv.int_,
            cv.Optional(CONF_IRQ_PIN, default=3): cv.int_,
            cv.Optional(CONF_RESET_PIN, default=1): cv.int_,
            cv.Optional(CONF_WAKE_PIN, default=2): cv.int_,
            cv.Optional(CONF_BUSY_PIN, default=5): cv.int_,
        }
    ),
    cv.only_on_esp32,
)


@coroutine_with_priority(60.0)  # Same priority as WiFi/Ethernet
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set component configuration
    cg.add(var.set_ssid(config[CONF_SSID]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_country_code(config[CONF_COUNTRY_CODE]))

    cg.add(var.set_spi_clk_pin(config[CONF_CLK_PIN]))
    cg.add(var.set_spi_mosi_pin(config[CONF_MOSI_PIN]))
    cg.add(var.set_spi_miso_pin(config[CONF_MISO_PIN]))
    cg.add(var.set_spi_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_spi_irq_pin(config[CONF_IRQ_PIN]))
    cg.add(var.set_reset_pin(config[CONF_RESET_PIN]))
    cg.add(var.set_wake_pin(config[CONF_WAKE_PIN]))
    cg.add(var.set_busy_pin(config[CONF_BUSY_PIN]))

    cg.add_define("USE_MM_HALOW")

    # Resolve MM-IoT-SDK path (upstream MorseMicro fork preferred, supports IDF >=5.1.1)
    sdk_path = os.path.expanduser(config[CONF_MM_IOT_SDK_PATH])
    sdk_path = os.path.abspath(sdk_path)
    framework_path = os.path.join(sdk_path, "framework")

    if not os.path.isdir(framework_path):
        raise cv.Invalid(
            f"MM-IoT-SDK not found at {sdk_path}. "
            f"Clone it with: git clone https://github.com/MorseMicro/mm-iot-esp32.git {sdk_path}"
        )

    # Register MM-IoT-SDK ESP-IDF components via local paths.
    # These directories each contain CMakeLists.txt + idf_component.yml
    # that the ESP-IDF build system understands natively.
    add_idf_component(
        name="morselib",
        path=os.path.join(framework_path, "morselib"),
    )
    add_idf_component(
        name="mm_shims",
        path=os.path.join(framework_path, "mm_shims"),
    )
    add_idf_component(
        name="mmipal",
        path=os.path.join(framework_path, "src", "mmipal"),
    )
    add_idf_component(
        name="mmutils",
        path=os.path.join(framework_path, "src", "mmutils"),
    )
    add_idf_component(
        name="mmpktmem",
        path=os.path.join(framework_path, "src", "mmpktmem"),
    )
    add_idf_component(
        name="mmregdb",
        path=os.path.join(framework_path, "src", "mmregdb"),
    )

    # Kconfig: Pin mapping for MM6108 SPI interface
    add_idf_sdkconfig_option("CONFIG_MM_RESET_N", config[CONF_RESET_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_WAKE", config[CONF_WAKE_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_BUSY", config[CONF_BUSY_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_SPI_SCK", config[CONF_CLK_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_SPI_MOSI", config[CONF_MOSI_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_SPI_MISO", config[CONF_MISO_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_SPI_CS", config[CONF_CS_PIN])
    add_idf_sdkconfig_option("CONFIG_MM_SPI_IRQ", config[CONF_IRQ_PIN])

    # Kconfig: Firmware and board configuration files
    # The upstream mm_shims CMakeLists.txt searches for these in morsefirmware/
    add_idf_sdkconfig_option("CONFIG_MM_BCF_FILE", "bcf_mf16858.mbin")
    add_idf_sdkconfig_option("CONFIG_MM_FW_FILE", "mm6108.mbin")

    # Kconfig: Chip type
    add_idf_sdkconfig_option("CONFIG_MMHAL_CHIP_TYPE_MM6108", True)

    # Kconfig: FreeRTOS settings required by MM-IoT-SDK
    add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)
    add_idf_sdkconfig_option("CONFIG_FREERTOS_TIMER_TASK_PRIORITY", 10)

    # Kconfig: ESP32-S3 cache optimization
    add_idf_sdkconfig_option("CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB", True)

    # Kconfig: MbedTLS option required by MM-IoT-SDK crypto shim
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_NIST_KW_C", True)

    # Kconfig: LWIP callbacks required by mmipal
    add_idf_sdkconfig_option("CONFIG_LWIP_NETIF_STATUS_CALLBACK", True)

    # LWIP_NETIF_LINK_CALLBACK is not exposed via Kconfig in ESP-IDF v5.5+,
    # so we set it directly as a compile definition
    cg.add_build_flag("-DLWIP_NETIF_LINK_CALLBACK=1")

    # The mm_shims CMakeLists uses objcopy custom commands to embed .mbin
    # firmware files. PlatformIO may not run these before linking. We add
    # a post-configure hook via extra_scripts to ensure the custom target
    # 'morsefirmware' is built before the main link step.
    cg.add_platformio_option(
        "extra_scripts",
        [os.path.join(os.path.dirname(__file__), "pre_build.py")],
    )

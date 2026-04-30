"""ESPHome component for Wi-Fi HaLow (IEEE 802.11ah) via Morse Micro MM6108."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_PASSWORD,
    CONF_SSID,
)
from esphome.core import coroutine_with_priority, CORE

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network"]
CONFLICTS_WITH = ["wifi"]

CONF_COUNTRY_CODE = "country_code"
CONF_SPI_CLK_PIN = "clk_pin"
CONF_SPI_MOSI_PIN = "mosi_pin"
CONF_SPI_MISO_PIN = "miso_pin"
CONF_SPI_CS_PIN = "cs_pin"
CONF_SPI_IRQ_PIN = "irq_pin"
CONF_RESET_PIN = "reset_pin"
CONF_WAKE_PIN = "wake_pin"
CONF_BUSY_PIN = "busy_pin"

mm_halow_ns = cg.esphome_ns.namespace("mm_halow")
MMHalowComponent = mm_halow_ns.class_("MMHalowComponent", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MMHalowComponent),
            cv.Required(CONF_SSID): cv.string,
            cv.Required(CONF_PASSWORD): cv.string,
            cv.Optional(CONF_COUNTRY_CODE, default="US"): cv.string_strict,
            # SPI pins - defaults match XIAO HaLow Hat
            cv.Optional(CONF_SPI_CLK_PIN, default=7): cv.int_,
            cv.Optional(CONF_SPI_MOSI_PIN, default=9): cv.int_,
            cv.Optional(CONF_SPI_MISO_PIN, default=8): cv.int_,
            cv.Optional(CONF_SPI_CS_PIN, default=4): cv.int_,
            cv.Optional(CONF_SPI_IRQ_PIN, default=3): cv.int_,
            cv.Optional(CONF_RESET_PIN, default=1): cv.int_,
            cv.Optional(CONF_WAKE_PIN, default=2): cv.int_,
            cv.Optional(CONF_BUSY_PIN, default=5): cv.int_,
        }
    ),
    cv.only_on_esp32,
)


@coroutine_with_priority(60.0)  # Same as WiFi/Ethernet
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_ssid(config[CONF_SSID]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_country_code(config[CONF_COUNTRY_CODE]))

    cg.add(var.set_spi_clk_pin(config[CONF_SPI_CLK_PIN]))
    cg.add(var.set_spi_mosi_pin(config[CONF_SPI_MOSI_PIN]))
    cg.add(var.set_spi_miso_pin(config[CONF_SPI_MISO_PIN]))
    cg.add(var.set_spi_cs_pin(config[CONF_SPI_CS_PIN]))
    cg.add(var.set_spi_irq_pin(config[CONF_SPI_IRQ_PIN]))
    cg.add(var.set_reset_pin(config[CONF_RESET_PIN]))
    cg.add(var.set_wake_pin(config[CONF_WAKE_PIN]))
    cg.add(var.set_busy_pin(config[CONF_BUSY_PIN]))

    cg.add_define("USE_MM_HALOW")

    # ESP-IDF sdkconfig options required by MM-IoT-SDK
    cg.add_platformio_option(
        "board_build.partitions", "default_8MB.csv"
    )

    # FreeRTOS config required by MM-IoT-SDK
    cg.add_platformio_option(
        "board_build.esp-idf.sdkconfig",
        [
            "CONFIG_FREERTOS_HZ=1000",
            "CONFIG_FREERTOS_TIMER_TASK_PRIORITY=10",
            "CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y",
        ],
    )

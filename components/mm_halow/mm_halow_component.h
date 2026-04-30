#pragma once

#include "esphome/core/component.h"
#include "esphome/components/network/ip_address.h"

#include <string>
#include <array>

namespace esphome {
namespace mm_halow {

class MMHalowComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool is_connected() const { return this->connected_; }
  network::IPAddresses get_ip_addresses() const { return this->ip_addresses_; }
  std::string get_ip_address_str() const;

  // Configuration setters (called from generated code)
  void set_ssid(const std::string &ssid) { this->ssid_ = ssid; }
  void set_password(const std::string &password) { this->password_ = password; }
  void set_country_code(const std::string &cc) { this->country_code_ = cc; }

  void set_spi_clk_pin(uint8_t pin) { this->spi_clk_pin_ = pin; }
  void set_spi_mosi_pin(uint8_t pin) { this->spi_mosi_pin_ = pin; }
  void set_spi_miso_pin(uint8_t pin) { this->spi_miso_pin_ = pin; }
  void set_spi_cs_pin(uint8_t pin) { this->spi_cs_pin_ = pin; }
  void set_spi_irq_pin(uint8_t pin) { this->spi_irq_pin_ = pin; }
  void set_reset_pin(uint8_t pin) { this->reset_pin_ = pin; }
  void set_wake_pin(uint8_t pin) { this->wake_pin_ = pin; }
  void set_busy_pin(uint8_t pin) { this->busy_pin_ = pin; }

 protected:
  void start_connect_();
  void check_ip_();

  std::string ssid_;
  std::string password_;
  std::string country_code_{"US"};

  // SPI pin config
  uint8_t spi_clk_pin_{7};
  uint8_t spi_mosi_pin_{9};
  uint8_t spi_miso_pin_{8};
  uint8_t spi_cs_pin_{4};
  uint8_t spi_irq_pin_{3};
  uint8_t reset_pin_{1};
  uint8_t wake_pin_{2};
  uint8_t busy_pin_{5};

  bool connected_{false};
  bool started_{false};
  bool got_ip_{false};
  uint32_t connect_start_time_{0};
  network::IPAddresses ip_addresses_{};
};

extern MMHalowComponent *global_mm_halow_component;  // NOLINT

}  // namespace mm_halow
}  // namespace esphome

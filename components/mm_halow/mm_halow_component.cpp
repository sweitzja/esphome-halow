/**
 * ESPHome component for Wi-Fi HaLow (IEEE 802.11ah) via Morse Micro MM6108.
 *
 * This wraps the MM-IoT-SDK to provide HaLow network connectivity within ESPHome.
 * The MM6108 communicates over SPI and requires firmware + BCF loaded at boot.
 *
 * Architecture:
 * - setup(): Initialize MM-IoT-SDK (mmhal, mmwlan), load firmware, start STA connection
 * - loop(): Poll for IP address changes, monitor link state
 * - The MM-IoT-SDK runs its own FreeRTOS tasks for SPI and WLAN management
 */

#include "mm_halow_component.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// MM-IoT-SDK headers
extern "C" {
#include "mmhal.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"
#include "mmregdb.h"
}

namespace esphome {
namespace mm_halow {

static const char *const TAG = "mm_halow";

MMHalowComponent *global_mm_halow_component = nullptr;  // NOLINT

// Callbacks from MM-IoT-SDK (called from SDK tasks, not main loop)
static volatile bool s_link_up = false;
static volatile bool s_sta_connected = false;

static void link_state_cb(enum mmwlan_link_state state, void *arg) {
  if (state == MMWLAN_LINK_UP) {
    s_link_up = true;
    ESP_LOGI(TAG, "HaLow link UP");
  } else {
    s_link_up = false;
    ESP_LOGW(TAG, "HaLow link DOWN");
  }
}

static void sta_status_cb(enum mmwlan_sta_state state) {
  const char *states[] = {"DISABLED", "CONNECTING", "CONNECTED"};
  ESP_LOGI(TAG, "STA state: %s", states[state]);
  s_sta_connected = (state == 2);  // MMWLAN_STA_CONNECTED
}

float MMHalowComponent::get_setup_priority() const {
  return setup_priority::WIFI;
}

void MMHalowComponent::setup() {
  ESP_LOGI(TAG, "Setting up Wi-Fi HaLow (MM6108)...");
  global_mm_halow_component = this;

  // TODO: Configure SPI pins via Kconfig overrides or mmhal API
  // For now, the MM-IoT-SDK uses its Kconfig defaults which match the XIAO HaLow Hat

  // Initialize MM-IoT-SDK subsystems
  mmhal_init();
  mmwlan_init();

  // Set regulatory domain
  const struct mmwlan_s1g_channel_list *channel_list =
      mmwlan_lookup_regulatory_domain(get_regulatory_db(), this->country_code_.c_str());
  if (channel_list == nullptr) {
    ESP_LOGE(TAG, "Invalid country code: %s", this->country_code_.c_str());
    this->mark_failed();
    return;
  }

  enum mmwlan_status status = mmwlan_set_channel_list(channel_list);
  if (status != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set channel list: %d", status);
    this->mark_failed();
    return;
  }

  // Register link state callback
  status = mmwlan_register_link_state_cb(link_state_cb, nullptr);
  if (status != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "Failed to register link state callback: %d", status);
    this->mark_failed();
    return;
  }

  // Boot the MM6108 (loads firmware + BCF over SPI)
  struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
  status = mmwlan_boot(&boot_args);
  if (status != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "MM6108 boot failed: %d", status);
    this->mark_failed();
    return;
  }

  // Read version info
  struct mmwlan_version version;
  status = mmwlan_get_version(&version);
  if (status == MMWLAN_SUCCESS) {
    ESP_LOGI(TAG, "Morse FW %s, morselib %s, chip 0x%lx",
             version.morse_fw_version, version.morselib_version, version.morse_chip_id);
  }

  // Read MAC address
  uint8_t mac[6];
  status = mmwlan_get_mac_addr(mac);
  if (status == MMWLAN_SUCCESS) {
    ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  // Initialize IP stack (LWIP via mmipal)
  struct mmipal_init_args ip_args = MMIPAL_INIT_ARGS_DEFAULT;
  ip_args.mode = MMIPAL_DHCP;
  enum mmipal_status ip_status = mmipal_init(&ip_args);
  if (ip_status != MMIPAL_SUCCESS) {
    ESP_LOGE(TAG, "IP stack init failed: %d", ip_status);
    this->mark_failed();
    return;
  }

  // Start STA connection
  this->start_connect_();
}

void MMHalowComponent::start_connect_() {
  struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;

  sta_args.ssid_len = this->ssid_.length();
  memcpy(sta_args.ssid, this->ssid_.c_str(), sta_args.ssid_len);

  sta_args.passphrase_len = this->password_.length();
  memcpy(sta_args.passphrase, this->password_.c_str(), sta_args.passphrase_len);

  sta_args.security_type = MMWLAN_SAE;

  ESP_LOGI(TAG, "Connecting to '%s'...", this->ssid_.c_str());

  enum mmwlan_status status = mmwlan_sta_enable(&sta_args, sta_status_cb);
  if (status != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "STA enable failed: %d", status);
    this->mark_failed();
    return;
  }

  this->started_ = true;
  this->connect_start_time_ = millis();
}

void MMHalowComponent::check_ip_() {
  struct mmipal_ip_config ip_config;
  enum mmipal_status status = mmipal_get_ip_config(&ip_config);
  if (status != MMIPAL_SUCCESS) {
    return;
  }

  // Check if we have a valid IP (not 0.0.0.0)
  if (strcmp(ip_config.ip_addr, "0.0.0.0") == 0) {
    return;
  }

  if (!this->got_ip_) {
    this->got_ip_ = true;
    this->ip_addresses_[0] = network::IPAddress(ip_config.ip_addr);
    ESP_LOGI(TAG, "Got IP: %s, Gateway: %s, Netmask: %s",
             ip_config.ip_addr, ip_config.gateway_addr, ip_config.netmask);
  }
}

void MMHalowComponent::loop() {
  bool link_up = s_link_up;

  if (link_up && !this->connected_) {
    this->connected_ = true;
    ESP_LOGI(TAG, "HaLow connected to '%s'", this->ssid_.c_str());
  } else if (!link_up && this->connected_) {
    this->connected_ = false;
    this->got_ip_ = false;
    this->ip_addresses_ = {};
    ESP_LOGW(TAG, "HaLow disconnected");
  }

  if (this->connected_ && !this->got_ip_) {
    this->check_ip_();
  }
}

void MMHalowComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wi-Fi HaLow (MM6108):");
  ESP_LOGCONFIG(TAG, "  SSID: '%s'", this->ssid_.c_str());
  ESP_LOGCONFIG(TAG, "  Country: %s", this->country_code_.c_str());
  ESP_LOGCONFIG(TAG, "  SPI Pins: CLK=%d MOSI=%d MISO=%d CS=%d",
                this->spi_clk_pin_, this->spi_mosi_pin_,
                this->spi_miso_pin_, this->spi_cs_pin_);
  ESP_LOGCONFIG(TAG, "  Control Pins: RESET=%d WAKE=%d IRQ=%d BUSY=%d",
                this->reset_pin_, this->wake_pin_,
                this->spi_irq_pin_, this->busy_pin_);
  if (this->is_connected()) {
    ESP_LOGCONFIG(TAG, "  IP: %s", this->get_ip_address_str().c_str());
  }
}

std::string MMHalowComponent::get_ip_address_str() const {
  if (this->ip_addresses_[0].is_set()) {
    char buf[64];
    this->ip_addresses_[0].str_to(buf);
    return std::string(buf);
  }
  return "0.0.0.0";
}

}  // namespace mm_halow
}  // namespace esphome

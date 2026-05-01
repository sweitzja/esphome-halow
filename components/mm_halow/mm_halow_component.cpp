/**
 * ESPHome component for Wi-Fi HaLow (IEEE 802.11ah) via Morse Micro MM6108.
 *
 * Wraps the MM-IoT-SDK to provide HaLow network connectivity within ESPHome.
 * The MM6108 communicates over SPI and requires firmware + BCF loaded at boot.
 *
 * State machine: STOPPED -> CONNECTING -> CONNECTED (with auto-reconnect)
 * SDK callbacks fire from FreeRTOS tasks; we use volatile flags polled in loop().
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
#ifdef USE_MM_HALOW_REGDB
#include "mmregdb.h"
#else
#include "mmwlan_regdb.def"
#endif
}

// ESP-IDF headers for mDNS integration
extern "C" {
#include "lwip/netif.h"
#include "mdns.h"
// Internal header for esp_netif_obj struct layout — needed to create
// a minimal wrapper around mmipal's raw LWIP netif for mDNS registration.
// The struct layout has been stable since ESP-IDF v4.x.
#include "esp_netif_lwip_internal.h"
}

namespace esphome {
namespace mm_halow {

static const char *const TAG = "mm_halow";

static const uint32_t CONNECT_TIMEOUT_MS = 60000;
static const uint32_t SENSOR_UPDATE_INTERVAL_MS = 10000;

MMHalowComponent *global_mm_halow_component = nullptr;  // NOLINT

// --- SDK Callbacks (called from MM-IoT-SDK FreeRTOS tasks) ---

static volatile bool s_link_up = false;
static volatile bool s_sta_connected = false;

static void link_state_cb(enum mmwlan_link_state state, void *arg) {
  s_link_up = (state == MMWLAN_LINK_UP);
  if (state == MMWLAN_LINK_UP) {
    ESP_LOGI(TAG, "HaLow link UP");
  } else {
    ESP_LOGW(TAG, "HaLow link DOWN");
  }
}

static void sta_status_cb(enum mmwlan_sta_state state) {
  const char *states[] = {"DISABLED", "CONNECTING", "CONNECTED"};
  ESP_LOGI(TAG, "STA state: %s", states[state]);
  s_sta_connected = (state == MMWLAN_STA_CONNECTED);
}

// --- Component Lifecycle ---

float MMHalowComponent::get_setup_priority() const {
  return setup_priority::WIFI;
}

void MMHalowComponent::set_manual_ip(const std::string &ip, const std::string &gw,
                                     const std::string &subnet, const std::string &dns1,
                                     const std::string &dns2) {
  this->use_static_ip_ = true;
  this->static_ip_ = ip;
  this->static_gw_ = gw;
  this->static_subnet_ = subnet;
  this->static_dns1_ = dns1;
  this->static_dns2_ = dns2;
}

void MMHalowComponent::setup() {
  ESP_LOGI(TAG, "Setting up Wi-Fi HaLow (MM6108)...");
  global_mm_halow_component = this;

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
  if (mmwlan_set_channel_list(channel_list) != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "Failed to set channel list");
    this->mark_failed();
    return;
  }

  // Register link state callback
  if (mmwlan_register_link_state_cb(link_state_cb, nullptr) != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "Failed to register link state callback");
    this->mark_failed();
    return;
  }

  // Boot the MM6108 (loads firmware + BCF over SPI)
  struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
  if (mmwlan_boot(&boot_args) != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "MM6108 boot failed");
    this->mark_failed();
    return;
  }

  // Disable power save — keeps radio awake so device responds to ARP/ping reliably.
  // Without this, the radio sleeps between beacons and misses inbound packets.
  mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);
  ESP_LOGI(TAG, "Power save disabled");

  // Read and log version info
  struct mmwlan_version version;
  if (mmwlan_get_version(&version) == MMWLAN_SUCCESS) {
    ESP_LOGI(TAG, "Morse FW %s, morselib %s, chip 0x%lx",
             version.morse_fw_version, version.morselib_version, version.morse_chip_id);
  }

  // Read and log MAC address
  uint8_t mac[6];
  if (mmwlan_get_mac_addr(mac) == MMWLAN_SUCCESS) {
    ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  // Initialize IP stack
  struct mmipal_init_args ip_args = MMIPAL_INIT_ARGS_DEFAULT;
  if (this->use_static_ip_) {
    ip_args.mode = MMIPAL_STATIC;
    strncpy(ip_args.ip_addr, this->static_ip_.c_str(), sizeof(ip_args.ip_addr) - 1);
    strncpy(ip_args.netmask, this->static_subnet_.c_str(), sizeof(ip_args.netmask) - 1);
    strncpy(ip_args.gateway_addr, this->static_gw_.c_str(), sizeof(ip_args.gateway_addr) - 1);
    ESP_LOGI(TAG, "Using static IP: %s", this->static_ip_.c_str());
  } else {
    ip_args.mode = MMIPAL_DHCP;
    ESP_LOGI(TAG, "Using DHCP");
  }
  if (mmipal_init(&ip_args) != MMIPAL_SUCCESS) {
    ESP_LOGE(TAG, "IP stack init failed");
    this->mark_failed();
    return;
  }

  this->setup_complete_ = true;
  this->state_ = HalowState::CONNECTING;
  this->start_connect_();
}

void MMHalowComponent::start_connect_() {
  struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;

  sta_args.ssid_len = this->ssid_.length();
  memcpy(sta_args.ssid, this->ssid_.c_str(), sta_args.ssid_len);

  if (this->security_type_ != "OPEN") {
    sta_args.passphrase_len = this->password_.length();
    memcpy(sta_args.passphrase, this->password_.c_str(), sta_args.passphrase_len);
  }

  if (this->security_type_ == "OWE") {
    sta_args.security_type = MMWLAN_OWE;
  } else if (this->security_type_ == "OPEN") {
    sta_args.security_type = MMWLAN_OPEN;
  } else {
    sta_args.security_type = MMWLAN_SAE;
  }

  ESP_LOGI(TAG, "Connecting to '%s' (%s)...", this->ssid_.c_str(), this->security_type_.c_str());

  enum mmwlan_status status = mmwlan_sta_enable(&sta_args, sta_status_cb);
  if (status != MMWLAN_SUCCESS) {
    ESP_LOGE(TAG, "STA enable failed: %d", status);
    return;
  }

  this->connect_start_time_ = millis();
}

bool MMHalowComponent::check_ip_() {
  struct mmipal_ip_config ip_config;
  if (mmipal_get_ip_config(&ip_config) != MMIPAL_SUCCESS) {
    return false;
  }
  if (strcmp(ip_config.ip_addr, "0.0.0.0") == 0) {
    return false;
  }
  network::IPAddress new_ip(ip_config.ip_addr);
  if (!this->ip_addresses_[0].is_set() || this->ip_addresses_[0] != new_ip) {
    this->ip_addresses_[0] = new_ip;
    ESP_LOGI(TAG, "Got IP: %s, Gateway: %s, Netmask: %s",
             ip_config.ip_addr, ip_config.gateway_addr, ip_config.netmask);
  }
  return true;
}

void MMHalowComponent::loop() {
  if (!this->setup_complete_)
    return;

  switch (this->state_) {
    case HalowState::CONNECTING: {
      // Accept connection if we have a valid IP — don't require link_state callback
      // which may not fire reliably on all AP configurations
      if (this->check_ip_()) {
        // Connected and got IP
        this->state_ = HalowState::CONNECTED;
        this->reconnect_count_ = 0;
        ESP_LOGI(TAG, "HaLow connected to '%s'", this->ssid_.c_str());
        this->last_sensor_update_ = 0;  // Force immediate sensor update

        // Ensure the LWIP netif link is marked UP so ARP responses work.
        // The mmipal link_up callback may not fire on all IDF/SDK combinations.
        uint8_t mac[6];
        mmwlan_get_mac_addr(mac);
        for (struct netif *nif = netif_list; nif != nullptr; nif = nif->next) {
          if (memcmp(nif->hwaddr, mac, 6) == 0) {
            if (!netif_is_link_up(nif)) {
              netif_set_link_up(nif);
              ESP_LOGI(TAG, "Forced LWIP netif link UP");
            }
            break;
          }
        }

        if (!this->mdns_started_) {
          this->start_mdns_();
        }
      } else if (millis() - this->connect_start_time_ > CONNECT_TIMEOUT_MS) {
        // Timeout — disable STA and schedule retry
        this->reconnect_count_++;
        ESP_LOGW(TAG, "Connect timeout, retrying (attempt %lu)...", this->reconnect_count_);
        mmwlan_sta_disable();
        // Non-blocking: set connect_start_time so we wait a bit before retrying
        this->connect_start_time_ = millis();
        this->state_ = HalowState::STOPPED;
      }
      break;
    }

    case HalowState::STOPPED: {
      // Wait 3 seconds after a failed attempt before retrying
      if (this->setup_complete_ && millis() - this->connect_start_time_ > 3000) {
        this->state_ = HalowState::CONNECTING;
        this->start_connect_();
      }
      break;
    }

    case HalowState::CONNECTED: {
      // Only consider disconnected if we've lost the IP AND the STA is not connected
      bool ip_valid = this->check_ip_();
      if (!ip_valid && !s_sta_connected) {
        // Link dropped — start reconnect
        this->state_ = HalowState::CONNECTING;
        this->ip_addresses_ = {};
        this->reconnect_count_++;
        ESP_LOGW(TAG, "Link lost, reconnecting (attempt %lu)...", this->reconnect_count_);
        mmwlan_sta_disable();
        this->start_connect_();
      } else {
        // Update sensors periodically
        uint32_t now = millis();
        if (now - this->last_sensor_update_ > SENSOR_UPDATE_INTERVAL_MS) {
          this->update_sensors_();
          this->last_sensor_update_ = now;
        }
      }
      break;
    }
  }
}

void MMHalowComponent::update_sensors_() {
#ifdef USE_SENSOR
  if (this->rssi_sensor_ != nullptr) {
    int32_t rssi = mmwlan_get_rssi();
    if (rssi != INT32_MIN) {
      this->rssi_sensor_->publish_state((float) rssi);
    }
  }
  if (this->tx_packets_sensor_ != nullptr || this->rx_packets_sensor_ != nullptr) {
    uint32_t tx = 0, rx = 0;
    mmipal_get_link_packet_counts(&tx, &rx);
    if (this->tx_packets_sensor_ != nullptr)
      this->tx_packets_sensor_->publish_state((float) tx);
    if (this->rx_packets_sensor_ != nullptr)
      this->rx_packets_sensor_->publish_state((float) rx);
  }
#endif

#ifdef USE_TEXT_SENSOR
  // Dynamic values — update every cycle
  if (this->ip_address_sensor_ != nullptr || this->gateway_sensor_ != nullptr ||
      this->subnet_sensor_ != nullptr) {
    struct mmipal_ip_config ip_config;
    if (mmipal_get_ip_config(&ip_config) == MMIPAL_SUCCESS) {
      if (this->ip_address_sensor_ != nullptr)
        this->ip_address_sensor_->publish_state(ip_config.ip_addr);
      if (this->gateway_sensor_ != nullptr)
        this->gateway_sensor_->publish_state(ip_config.gateway_addr);
      if (this->subnet_sensor_ != nullptr)
        this->subnet_sensor_->publish_state(ip_config.netmask);
    }
  }
  if (this->bssid_sensor_ != nullptr) {
    uint8_t bssid[6];
    if (mmwlan_get_bssid(bssid) == MMWLAN_SUCCESS) {
      char buf[18];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      this->bssid_sensor_->publish_state(buf);
    }
  }

  // Static values — publish once
  if (this->ssid_sensor_ != nullptr && this->ssid_sensor_->get_raw_state().empty()) {
    this->ssid_sensor_->publish_state(this->ssid_);
  }
  if (this->mac_address_sensor_ != nullptr && this->mac_address_sensor_->get_raw_state().empty()) {
    uint8_t mac[6];
    if (mmwlan_get_mac_addr(mac) == MMWLAN_SUCCESS) {
      char buf[18];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      this->mac_address_sensor_->publish_state(buf);
    }
  }
  if (this->fw_version_sensor_ != nullptr && this->fw_version_sensor_->get_raw_state().empty()) {
    struct mmwlan_version ver;
    if (mmwlan_get_version(&ver) == MMWLAN_SUCCESS) {
      this->fw_version_sensor_->publish_state(ver.morse_fw_version);
    }
  }
#endif
}

void MMHalowComponent::start_mdns_() {
  // Find mmipal's LWIP netif by matching HaLow MAC address
  uint8_t mac[6];
  if (mmwlan_get_mac_addr(mac) != MMWLAN_SUCCESS) {
    ESP_LOGW(TAG, "mDNS: could not get MAC address");
    return;
  }

  struct netif *mm_netif = nullptr;
  for (struct netif *nif = netif_list; nif != nullptr; nif = nif->next) {
    if (memcmp(nif->hwaddr, mac, 6) == 0) {
      mm_netif = nif;
      break;
    }
  }
  if (mm_netif == nullptr) {
    ESP_LOGW(TAG, "mDNS: could not find HaLow LWIP netif");
    return;
  }

  // Create a minimal esp_netif_obj wrapper around the raw LWIP netif.
  // mdns_register_netif() stores this pointer and later calls
  // esp_netif_get_ip_info() which reads from esp_netif->lwip_netif.
  auto *fake = (struct esp_netif_obj *) calloc(1, sizeof(struct esp_netif_obj));
  if (fake == nullptr) {
    ESP_LOGW(TAG, "mDNS: alloc failed");
    return;
  }
  fake->lwip_netif = mm_netif;
  fake->flags = (esp_netif_flags_t)(ESP_NETIF_FLAG_AUTOUP);
  fake->if_key = strdup("MM_HALOW_DEF");
  fake->if_desc = strdup("mm_halow");
  fake->route_prio = 50;

  // Initialize mDNS. ESPHome's mdns component may have already tried and failed
  // (because there was no esp_netif registered at that time). We free any failed
  // state and reinitialize.
  mdns_free();  // Clean up ESPHome's failed init attempt
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    free(fake->if_key);
    free(fake->if_desc);
    free(fake);
    return;
  }

  // Set hostname
  mdns_hostname_set(App.get_name().c_str());

  // Register our fake netif with mDNS
  err = mdns_register_netif((esp_netif_t *) fake);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS register netif failed: %s", esp_err_to_name(err));
    free(fake->if_key);
    free(fake->if_desc);
    free(fake);
    return;
  }

  // Enable mDNS on this interface
  err = mdns_netif_action((esp_netif_t *) fake, MDNS_EVENT_ENABLE_IP4);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS enable IPv4 failed: %s", esp_err_to_name(err));
  }

  // Announce the IP
  err = mdns_netif_action((esp_netif_t *) fake, MDNS_EVENT_ANNOUNCE_IP4);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS announce failed: %s", esp_err_to_name(err));
  }

  ESP_LOGI(TAG, "mDNS: registered '%s.local' on HaLow interface", App.get_name().c_str());
  this->mdns_started_ = true;
}

void MMHalowComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wi-Fi HaLow (MM6108):");
  ESP_LOGCONFIG(TAG, "  SSID: '%s'", this->ssid_.c_str());
  ESP_LOGCONFIG(TAG, "  Security: %s", this->security_type_.c_str());
  ESP_LOGCONFIG(TAG, "  Country: %s", this->country_code_.c_str());
  ESP_LOGCONFIG(TAG, "  SPI Pins: CLK=%d MOSI=%d MISO=%d CS=%d",
                this->spi_clk_pin_, this->spi_mosi_pin_,
                this->spi_miso_pin_, this->spi_cs_pin_);
  ESP_LOGCONFIG(TAG, "  Control Pins: RESET=%d WAKE=%d IRQ=%d BUSY=%d",
                this->reset_pin_, this->wake_pin_,
                this->spi_irq_pin_, this->busy_pin_);
  if (this->use_static_ip_) {
    ESP_LOGCONFIG(TAG, "  Static IP: %s", this->static_ip_.c_str());
    ESP_LOGCONFIG(TAG, "  Gateway: %s", this->static_gw_.c_str());
    ESP_LOGCONFIG(TAG, "  Subnet: %s", this->static_subnet_.c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  IP: DHCP");
  }
  if (this->is_connected()) {
    ESP_LOGCONFIG(TAG, "  Current IP: %s", this->get_ip_address_str().c_str());
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

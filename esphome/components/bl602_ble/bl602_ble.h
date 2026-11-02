#pragma once
#include "esphome/core/defines.h"
#ifdef USE_BL602_BLE
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <cstdint>

namespace esphome::bl602_ble {
struct BLEScanReport {
  uint8_t mac[6];  // controller order, least-significant octet first
  int8_t rssi;
  uint8_t addr_type;
  bool is_scan_response;
  bool scannable;
  uint8_t data_len;
  uint8_t data[62];
};
class BLEScanListener {
 public:
  virtual void on_scan_report(const BLEScanReport &report) = 0;
 protected:
  ~BLEScanListener() = default;
};
class BL602BLE final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  bool is_ready() const { return this->ready_; }
  bool is_scanning() const { return this->scanning_; }
  bool is_starting() const { return this->starting_; }
  void get_mac_lsb_first(uint8_t out[6]) const;
#ifdef BL602_BLE_SCAN_LISTENER_COUNT
  void register_scan_listener(BLEScanListener *listener) { this->listeners_.push_back(listener); }
#endif
  bool scan_start(uint16_t interval, uint16_t window, bool active);
  bool scan_stop();
 protected:
#ifdef BL602_BLE_SCAN_LISTENER_COUNT
  StaticVector<BLEScanListener *, BL602_BLE_SCAN_LISTENER_COUNT> listeners_;
#endif
  bool ready_{false};
  bool scanning_{false};
  bool starting_{false};
  bool stop_pending_{false};
  uint32_t diagnostic_log_at_{0};
  uint32_t tracker_reports_{0};
  bool initialization_started_{false};
  static void initialization_task_(void *param);
};
}  // namespace esphome::bl602_ble
#endif

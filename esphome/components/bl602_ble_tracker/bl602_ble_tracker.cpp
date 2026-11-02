#ifdef USE_LIBRETINY
#include "bl602_ble_tracker.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::bl602_ble_tracker {
ESPHOME_LOG_TAG(TAG, "bl602_ble_tracker");
void BL602BLETracker::setup() {
  this->parent_->register_scan_listener(this);
  this->merger_.bind(&this->dispatcher_, &this->scan_continuous_, TAG);
  this->wanted_ = this->wanted_ || this->scan_continuous_;
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
}
#ifdef USE_OTA_STATE_LISTENER
void BL602BLETracker::on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent *) {
  if (state == ota::OTA_STARTED) {
    this->wanted_before_ota_ = this->wanted_;
    this->wanted_ = false;
    this->parent_->scan_stop();
    this->scan_running_ = false;
    this->merger_.flush();
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    this->wanted_ = this->wanted_before_ota_;
    this->wanted_before_ota_ = false;
    this->retry_at_ = 0;
  }
}
#endif
void BL602BLETracker::loop() {
  const uint32_t now = millis();
  if (!this->merger_.empty()) this->merger_.sweep(now);
  if (!this->wanted_) return;
  if (this->parent_->is_scanning() && !this->scan_running_) {
    this->scan_running_ = true;
    this->scan_start_time_ = this->scan_period_start_ = now;
  }
  if (!this->parent_->is_scanning()) {
    this->scan_running_ = false;
    if (this->parent_->is_ready() && !this->parent_->is_starting() &&
        (this->retry_at_ == 0 || static_cast<int32_t>(now - this->retry_at_) >= 0)) {
      this->retry_at_ = now + 1000;
      this->parent_->scan_start(this->scan_interval_, this->scan_window_, this->scan_active_);
    }
    return;
  }
  if (now - this->scan_period_start_ < this->scan_duration_) return;
  this->merger_.flush();
  this->dispatcher_.on_scan_end();
  this->scan_period_start_ = now;
  if (!this->scan_continuous_) this->stop_scan();
}
void BL602BLETracker::start_scan() { this->wanted_ = true; this->retry_at_ = 0; this->enable_loop(); }
void BL602BLETracker::stop_scan() {
  this->wanted_ = false;
  this->scan_continuous_ = false;
  this->parent_->scan_stop();
  this->scan_running_ = false;
  this->merger_.flush();
}
void BL602BLETracker::restart_scan_duration() { this->scan_period_start_ = millis(); }
bool BL602BLETracker::request_scan_mode(bool active) {
  // Runtime mode switching requires an asynchronous stop/restart. Until its
  // completion semantics are proven, refuse changes rather than report a
  // mode the radio has not applied.
  return active == this->scan_active_;
}

void BL602BLETracker::on_scan_report(const bl602_ble::BLEScanReport &report) {
  if (!this->wanted_) return;
  if (report.is_scan_response) {
    this->merger_.submit_scan_rsp(report.mac, report.rssi, report.addr_type, report.data, report.data_len);
  } else if (this->scan_active_ && report.scannable) {
    this->merger_.stash_adv(report.mac, report.rssi, report.addr_type, report.data, report.data_len, millis());
  } else {
    this->dispatcher_.dispatch(report.mac, report.rssi, report.addr_type, report.data, report.data_len, false,
                               this->scan_continuous_ ? nullptr : TAG);
  }
}
void BL602BLETracker::dump_config() { ESP_LOGCONFIG(TAG, "BL602 BLE Tracker (experimental)"); }
}  // namespace esphome::bl602_ble_tracker
#endif

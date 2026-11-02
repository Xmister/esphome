#include "bl602_ble.h"
#ifdef USE_BL602_BLE
#include "esphome/core/log.h"
#include <FreeRTOS.h>
#include <task.h>
#include <cstring>
extern "C" {
#include <lt_bl602_ble.h>
#include <lt_bl602_ble_queue.h>
int lt_bl602_wifi_manager_ready_for_ble(void) __attribute__((weak));
}
namespace esphome::bl602_ble {
static const char *const TAG = "bl602_ble";

static constexpr uint32_t BLE_INIT_STACK_WORDS = 2048;
static constexpr UBaseType_t BLE_INIT_TASK_PRIORITY = tskIDLE_PRIORITY + 2;
static StaticTask_t ble_init_tcb;
static StackType_t ble_init_stack[BLE_INIT_STACK_WORDS];

void BL602BLE::setup() {
  const int error = bl602_ble_queue_init();
  if (error) { ESP_LOGE(TAG, "BLE event queue init failed: %d", error); this->mark_failed(); return; }
}

void BL602BLE::initialization_task_(void *param) {
  (void) param;
  const int error = bl602_ble_prepare(bl602_ble_queue_ready);
  if (error) {
    ESP_LOGE(TAG, "BLE stack initialization failed: %d", error);
    bl602_ble_queue_ready(error);
  }
  vTaskDelete(nullptr);
}

void BL602BLE::get_mac_lsb_first(uint8_t out[6]) const {
  if (bl602_ble_get_address(out) != 0) memset(out, 0, 6);
}
bool BL602BLE::scan_start(uint16_t interval, uint16_t window, bool active) {
  if (!this->ready_ || this->starting_ || this->scanning_) return false;
  const bl602_scan_config config{interval, window, static_cast<uint8_t>(active)};
  this->starting_ = true;
  bl602_ble_queue_begin_scan();
  const int error = bl602_scan_start_configured(&config, bl602_ble_queue_advertisement, bl602_ble_queue_ready);
  if (error) { bl602_ble_queue_end_scan(); this->starting_ = false; ESP_LOGE(TAG, "BLE scan start failed: %d", error); return false; }
  this->starting_ = false;
  this->scanning_ = true;
  return true;
}
bool BL602BLE::scan_stop() {
  if (this->starting_) { bl602_ble_queue_end_scan(); this->stop_pending_ = true; return true; }
  if (!this->scanning_) return true;
  int error = bl602_scan_stop();
  if (error) { ESP_LOGE(TAG, "BLE scan stop failed: %d", error); return false; }
  bl602_ble_queue_end_scan();
  this->scanning_ = false;
  return true;
}
void BL602BLE::loop() {
  bl602_ble_event event;
  for (unsigned i = 0; i < BL602_BLE_QUEUE_LENGTH + 1 && bl602_ble_queue_poll(&event) == 1; i++) {
    if (event.type == BL602_BLE_EVENT_READY) {
      if (event.status) { ESP_LOGE(TAG, "BLE stack initialization failed: %d", event.status); this->mark_failed(); }
      else this->ready_ = true;
      continue;
    }
    if (!this->scanning_) continue;
#ifdef BL602_BLE_SCAN_LISTENER_COUNT
    this->tracker_reports_++;
    BLEScanReport report{};
    memcpy(report.mac, event.address, sizeof(report.mac));
    report.rssi = event.rssi; report.addr_type = event.address_type;
    report.is_scan_response = event.event_type == 4;
    report.scannable = event.event_type == 0 || event.event_type == 2;
    report.data_len = event.data_len; memcpy(report.data, event.data, event.data_len);
    for (auto *listener : this->listeners_) listener->on_scan_report(report);
#endif
  }

  const uint32_t now = millis();
  if (this->scanning_ && now - this->diagnostic_log_at_ >= 5000) {
    this->diagnostic_log_at_ = now;
    bl602_ble_scan_stats_t controller{};
    bl602_ble_queue_stats_t queue{};
    (void) bl602_ble_get_scan_stats(&controller);
    bl602_ble_queue_get_stats(&queue);
    ESP_LOGI(TAG, "scan stats controller=%u queue_cb=%u queued=%u dropped=%u polled=%u tracker=%u",
             static_cast<unsigned>(controller.controller_reports), static_cast<unsigned>(queue.callback_reports),
             static_cast<unsigned>(queue.queued_reports), static_cast<unsigned>(queue.dropped_reports),
             static_cast<unsigned>(queue.polled_reports), static_cast<unsigned>(this->tracker_reports_));
  }

  if (this->initialization_started_ || this->is_failed() ||
      (lt_bl602_wifi_manager_ready_for_ble && !lt_bl602_wifi_manager_ready_for_ble()))
    return;

  this->initialization_started_ = true;
  if (xTaskCreateStatic(&BL602BLE::initialization_task_, "ble_init", BLE_INIT_STACK_WORDS, nullptr,
                        BLE_INIT_TASK_PRIORITY, ble_init_stack, &ble_init_tcb) == nullptr) {
    ESP_LOGE(TAG, "Failed to create BLE initialization task");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Starting BLE initialization after Wi-Fi is ready");
}
void BL602BLE::dump_config() { ESP_LOGCONFIG(TAG, "BL602 BLE controller (experimental)"); }
}  // namespace esphome::bl602_ble
#endif

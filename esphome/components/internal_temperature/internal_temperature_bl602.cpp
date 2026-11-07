#ifdef USE_BL60X

#include "esphome/core/log.h"
#include "internal_temperature.h"

#include <cmath>

extern "C" {
int bl_tsen_adc_get(int16_t *temp, uint8_t log_flag);
}

namespace esphome::internal_temperature {

ESPHOME_LOG_TAG(TAG, "internal_temperature");

void InternalTemperatureSensor::update() {
  int16_t raw_temperature = 0;
  const int result = bl_tsen_adc_get(&raw_temperature, 0);
  const bool success = result == 0;
  const float temperature = raw_temperature;

  if (success && std::isfinite(temperature)) {
    this->publish_state(temperature);
  } else {
    ESP_LOGD(TAG, "Ignoring invalid temperature (success=%d, value=%.1f)", success, temperature);
    if (!this->has_state()) {
      this->publish_state(NAN);
    }
  }
}

}  // namespace esphome::internal_temperature

#endif  // USE_BL60X

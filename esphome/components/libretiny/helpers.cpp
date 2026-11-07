#include "esphome/core/helpers.h"

#ifdef USE_LIBRETINY

#include "esphome/core/hal.h"

#include <WiFi.h>  // for macAddress()

namespace esphome {

bool random_bytes(uint8_t *data, size_t len) {
  lt_rand_bytes(data, len);
  return true;
}

// only affects the executing core
// so should not be used as a mutex lock, only to get accurate timing
#ifdef USE_LIBRETINY_VARIANT_BL60X
// The BL602 FreeRTOS port's portDISABLE_INTERRUPTS()/portENABLE_INTERRUPTS()
// unconditionally clear/set MSTATUS.MIE. Save the old bit atomically so a
// nested lock does not re-enable interrupts while its outer lock is active,
// and so a lock entered with interrupts already disabled restores that state.
IRAM_ATTR InterruptLock::InterruptLock() {
  uint32_t mie_mask = 1U << 3;
  asm volatile("csrrc %0, mstatus, %1" : "=r"(state_) : "r"(mie_mask) : "memory");
}
IRAM_ATTR InterruptLock::~InterruptLock() {
  if (state_ & (1U << 3)) {
    uint32_t mie_mask = 1U << 3;
    asm volatile("csrs mstatus, %0" : : "r"(mie_mask) : "memory");
  }
}
#else
IRAM_ATTR InterruptLock::InterruptLock() { portDISABLE_INTERRUPTS(); }
IRAM_ATTR InterruptLock::~InterruptLock() { portENABLE_INTERRUPTS(); }
#endif

// LibreTiny LwIPLock is defined inline as a no-op in helpers.h

void get_mac_address_raw(uint8_t *mac) {  // NOLINT(readability-non-const-parameter)
  WiFi.macAddress(mac);
}

}  // namespace esphome

#endif  // USE_LIBRETINY

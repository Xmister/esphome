#pragma once

#ifdef USE_LIBRETINY_VARIANT_BL60X
#include "ota_backend.h"
#include <sodium/crypto_hash_sha256.h>

namespace esphome::ota {

/** OTA receiver for the verified Sonoff MINIR3 BL602 partition layout.
 *
 * ESPHome sends a 512-byte SDK package header followed by a bootinfo+app
 * payload. The SDK OTA API receives the payload followed by its own SHA256
 * trailer. Writes are staged on 4 KiB boundaries because hosal_ota_update()
 * erases sectors for each call.
 */
class BL60XOTABackend final {
 public:
  OTAResponseTypes begin(size_t image_size, OTAType ota_type = OTA_TYPE_UPDATE_APP);
  void set_update_md5(const char *md5);
  OTAResponseTypes write(uint8_t *data, size_t len);
  OTAResponseTypes end();
  void abort();
  static constexpr bool supports_compression() { return false; }

 private:
  bool process_package_header_();
  bool process_bootinfo_();
  bool flush_block_(bool final = false);
  void reset_();

  UpdateLock lock_;
  size_t expected_image_size_{0};
  size_t received_{0};
  uint32_t payload_size_{0};
  uint32_t payload_received_{0};
  uint32_t app_size_{0};
  uint32_t app_received_{0};
  uint8_t package_header_[512]{};
  size_t package_header_received_{0};
  uint8_t block_[4096]{};
  size_t block_received_{0};
  uint8_t expected_payload_sha_[32]{};
  uint8_t expected_app_sha_[32]{};
  uint8_t app_sha_digest_[32]{};
  uint8_t sdk_sha_digest_[32]{};
  // BL602's hardware SHA driver requires aligned input; OTA chunks may be unaligned.
  crypto_hash_sha256_state payload_sha_{};
  crypto_hash_sha256_state app_sha_{};
  bool sdk_ota_started_{false};
  bool package_header_valid_{false};
  bool bootinfo_valid_{false};
};

std::unique_ptr<BL60XOTABackend> make_ota_backend();

}  // namespace esphome::ota
#endif

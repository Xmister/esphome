#ifdef USE_LIBRETINY_VARIANT_BL60X
#include "ota_backend_bl60x.h"

#include "esphome/core/log.h"

extern "C" {
#include "hal_boot2.h"
#include "hal_sys.h"
#include "hosal_ota.h"
}

#include <algorithm>
#include <cstring>

namespace esphome::ota {

ESPHOME_LOG_TAG(TAG, "ota.bl60x");
static constexpr size_t PACKAGE_HEADER_SIZE = 512;
static constexpr size_t BOOTINFO_SIZE = 4096;
static constexpr size_t INTERNAL_SHA_SIZE = 32;
static constexpr size_t MINIR3_INACTIVE_SLOT_SIZE = 0xB4000;
static constexpr uint8_t OTA_MAGIC[] = "BL60X_OTA_Ver1.0";
static constexpr uint8_t BFNP_MAGIC[] = "BFNP";

static uint32_t read_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

static bool constant_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++)
    diff |= a[i] ^ b[i];
  return diff == 0;
}

std::unique_ptr<BL60XOTABackend> make_ota_backend() { return make_unique<BL60XOTABackend>(); }

OTAResponseTypes BL60XOTABackend::begin(size_t image_size, OTAType ota_type) {
  if (!this->lock_.try_acquire())
    return OTA_RESPONSE_ERROR_UPDATE_BUSY;
  if (ota_type != OTA_TYPE_UPDATE_APP)
    return OTA_RESPONSE_ERROR_UNSUPPORTED_OTA_TYPE;
  // Unknown length is unsafe: the SDK receiver and MINIR3 inactive slot need
  // an exact bounded image length before any partition operation can begin.
  if (image_size < PACKAGE_HEADER_SIZE + BOOTINFO_SIZE ||
      image_size > PACKAGE_HEADER_SIZE + MINIR3_INACTIVE_SLOT_SIZE - INTERNAL_SHA_SIZE)
    return OTA_RESPONSE_ERROR_ESP32_NOT_ENOUGH_SPACE;

  this->expected_image_size_ = image_size;
  if (crypto_hash_sha256_init(&this->payload_sha_) != 0 || crypto_hash_sha256_init(&this->app_sha_) != 0)
    return OTA_RESPONSE_ERROR_UNKNOWN;
  return OTA_RESPONSE_OK;
}

// The OTA envelope and bootinfo include SHA-256 digests for the payload and
// application. Do not use the LibreTiny Arduino MD5 adapter here: the pinned
// BL602 SDK excludes its MD5 implementation, and SHA-256 is the integrity
// check actually used by this backend and the ROM/SDK updater.
void BL60XOTABackend::set_update_md5(const char *md5) { (void) md5; }

bool BL60XOTABackend::process_package_header_() {
  const auto *h = this->package_header_;
  if (memcmp(h, OTA_MAGIC, sizeof(OTA_MAGIC) - 1) != 0 || memcmp(h + 16, "RAW ", 4) != 0) {
    ESP_LOGE(TAG, "Invalid BL60X OTA package magic or format");
    return false;
  }
  this->payload_size_ = read_le32(h + 20);
  if (read_le32(h + 96) != this->payload_size_ ||
      this->expected_image_size_ != PACKAGE_HEADER_SIZE + this->payload_size_ ||
      this->payload_size_ < BOOTINFO_SIZE ||
      this->payload_size_ + INTERNAL_SHA_SIZE > MINIR3_INACTIVE_SLOT_SIZE) {
    ESP_LOGE(TAG, "Invalid OTA lengths: image=%u payload=%u",
             (unsigned) this->expected_image_size_, (unsigned) this->payload_size_);
    return false;
  }
  if (memcmp(h + 32, "MINIR3", 6) != 0) {
    ESP_LOGE(TAG, "OTA package hardware target is not MINIR3");
    return false;
  }
  memcpy(this->expected_payload_sha_, h + 64, sizeof(this->expected_payload_sha_));
  this->package_header_valid_ = true;
  return true;
}

bool BL60XOTABackend::process_bootinfo_() {
  const auto *b = this->block_;
  if (memcmp(b, BFNP_MAGIC, sizeof(BFNP_MAGIC) - 1) != 0 ||
      crc32(b, 172) != read_le32(b + 172) || (b[116] & 0x0F) != 0) {
    ESP_LOGE(TAG, "Invalid BL602 bootinfo header");
    return false;
  }
  this->app_size_ = read_le32(b + 120);
  if (this->app_size_ == 0 || this->app_size_ != this->payload_size_ - BOOTINFO_SIZE) {
    ESP_LOGE(TAG, "Invalid app size: header=%u payload=%u", (unsigned) this->app_size_,
             (unsigned) this->payload_size_);
    return false;
  }
  memcpy(this->expected_app_sha_, b + 132, sizeof(this->expected_app_sha_));

  // The partition table on the verified MINIR3 overlaps the inactive FW
  // slot with the compressed factory fallback. The SDK explicitly removes
  // that entry before writing; call it only after the complete OTA envelope
  // and boot header have passed validation.
  hal_update_mfg_ptable();
  const int start_result = hosal_ota_start(this->payload_size_ + INTERNAL_SHA_SIZE);
  if (start_result != 0) {
    ESP_LOGE(TAG, "SDK OTA start failed: %d", start_result);
    return false;
  }
  this->sdk_ota_started_ = true;
  this->bootinfo_valid_ = true;
  return true;
}

bool BL60XOTABackend::flush_block_(bool final) {
  if (!this->block_received_)
    return true;
  if (this->payload_received_ == BOOTINFO_SIZE && !this->bootinfo_valid_ && !this->process_bootinfo_()) {
    ESP_LOGE(TAG, "Could not validate bootinfo/start SDK OTA");
    return false;
  }
  if (!this->sdk_ota_started_) {
    ESP_LOGE(TAG, "SDK OTA write requested before SDK OTA start");
    return false;
  }
  if (hosal_ota_update(this->payload_size_ + INTERNAL_SHA_SIZE, this->payload_received_, this->block_,
                       this->block_received_) != 0) {
    ESP_LOGE(TAG, "SDK write failed at offset %u (%u bytes)", (unsigned) this->payload_received_,
             (unsigned) this->block_received_);
    return false;
  }
  this->payload_received_ += this->block_received_;
  this->block_received_ = 0;
  (void) final;
  return true;
}

OTAResponseTypes BL60XOTABackend::write(uint8_t *data, size_t len) {
  if (data == nullptr || len == 0 || this->received_ + len > this->expected_image_size_) {
    ESP_LOGE(TAG, "Invalid write: data=%u len=%u received=%u/%u", data != nullptr, (unsigned) len,
             (unsigned) this->received_, (unsigned) this->expected_image_size_);
    return OTA_RESPONSE_ERROR_WRITING_FLASH;
  }
  size_t offset = 0;
  while (offset < len) {
    if (this->package_header_received_ < PACKAGE_HEADER_SIZE) {
      const size_t take = std::min(len - offset, PACKAGE_HEADER_SIZE - this->package_header_received_);
      memcpy(this->package_header_ + this->package_header_received_, data + offset, take);
      this->package_header_received_ += take;
      this->received_ += take;
      offset += take;
      if (this->package_header_received_ == PACKAGE_HEADER_SIZE && !this->process_package_header_())
        return OTA_RESPONSE_ERROR_MAGIC;
      continue;
    }

    const size_t take = std::min(len - offset, sizeof(this->block_) - this->block_received_);
    memcpy(this->block_ + this->block_received_, data + offset, take);
    if (crypto_hash_sha256_update(&this->payload_sha_, data + offset, take) != 0)
      return OTA_RESPONSE_ERROR_WRITING_FLASH;
    const uint32_t block_start = this->payload_received_ + this->block_received_;
    const uint32_t app_start = block_start < BOOTINFO_SIZE ? BOOTINFO_SIZE : block_start;
    const uint32_t app_end = std::min<uint32_t>(block_start + take, BOOTINFO_SIZE + this->app_size_);
    if (app_end > app_start &&
        crypto_hash_sha256_update(&this->app_sha_, data + offset + (app_start - block_start), app_end - app_start) != 0)
      return OTA_RESPONSE_ERROR_WRITING_FLASH;
    this->block_received_ += take;
    this->received_ += take;
    offset += take;

    if (this->block_received_ == sizeof(this->block_)) {
      if (this->payload_received_ == 0 && !this->process_bootinfo_())
        return OTA_RESPONSE_ERROR_MAGIC;
      if (!this->flush_block_())
        return OTA_RESPONSE_ERROR_WRITING_FLASH;
    }
  }
  return OTA_RESPONSE_OK;
}

OTAResponseTypes BL60XOTABackend::end() {
  if (this->received_ != this->expected_image_size_ || !this->package_header_valid_ ||
      this->payload_received_ + this->block_received_ != this->payload_size_) {
    ESP_LOGE(TAG, "Incomplete image: received=%u/%u payload=%u+%u/%u",
             (unsigned) this->received_, (unsigned) this->expected_image_size_, (unsigned) this->payload_received_,
             (unsigned) this->block_received_, (unsigned) this->payload_size_);
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }
  if (this->block_received_ && !this->flush_block_(true))
    return OTA_RESPONSE_ERROR_WRITING_FLASH;
  if (!this->bootinfo_valid_ || this->payload_received_ != this->payload_size_) {
    ESP_LOGE(TAG, "Invalid bootinfo or payload length: bootinfo=%d payload=%u/%u", this->bootinfo_valid_,
             (unsigned) this->payload_received_, (unsigned) this->payload_size_);
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }

  if (crypto_hash_sha256_final(&this->payload_sha_, this->sdk_sha_digest_) != 0)
    return OTA_RESPONSE_ERROR_UPDATE_END;
  if (!constant_equal(this->sdk_sha_digest_, this->expected_payload_sha_, sizeof(this->sdk_sha_digest_))) {
    ESP_LOGE(TAG, "OTA package SHA-256 mismatch");
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }
  if (crypto_hash_sha256_final(&this->app_sha_, this->app_sha_digest_) != 0)
    return OTA_RESPONSE_ERROR_UPDATE_END;
  if (!constant_equal(this->app_sha_digest_, this->expected_app_sha_, sizeof(this->app_sha_digest_))) {
    ESP_LOGE(TAG, "BFNP application SHA-256 mismatch");
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }
  const int trailer_result = hosal_ota_update(this->payload_size_ + INTERNAL_SHA_SIZE, this->payload_size_,
                                               this->sdk_sha_digest_, INTERNAL_SHA_SIZE);
  if (trailer_result != 0) {
    ESP_LOGE(TAG, "SDK SHA trailer write failed: %d", trailer_result);
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }

  // The SDK finalizer frees its private OTA state with aos_free() after the
  // partition-table update. On this BL602 build that path traps in the
  // FreeRTOS heap, even when the SDK's optional second SHA pass is disabled.
  // We already checked both SHA-256 values and wrote the SDK trailer above,
  // so perform the same boot-slot update directly. The MTD handle and SDK
  // OTA state are intentionally left for the immediate ESPHome reboot to
  // reclaim; closing/freeing them here re-enters the failing heap path.
  HALPartition_Entry_Config fw_entry{};
  if (hal_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &fw_entry) != 0) {
    ESP_LOGE(TAG, "Could not read active firmware partition table");
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }
  fw_entry.len = this->payload_size_;
  const int finish_result = hal_boot2_update_ptable(&fw_entry);
  if (finish_result != 0) {
    ESP_LOGE(TAG, "Firmware partition-table update failed: %d", finish_result);
    return OTA_RESPONSE_ERROR_UPDATE_END;
  }
  ESP_LOGI(TAG, "Firmware slot selected; reboot will reclaim the SDK OTA handle");
  this->sdk_ota_started_ = false;
  this->lock_.hold_until_reboot();
  return OTA_RESPONSE_OK;
}

void BL60XOTABackend::abort() {
  if (this->sdk_ota_started_) {
    // The pinned SDK has no OTA abort/close function; reboot clears its static
    // handle while the active partition remains selected.
    hal_reboot();
    return;
  }
  this->reset_();
}

void BL60XOTABackend::reset_() {
  this->expected_image_size_ = 0;
  this->received_ = 0;
  this->payload_size_ = 0;
  this->payload_received_ = 0;
  this->app_size_ = 0;
  this->app_received_ = 0;
  this->package_header_received_ = 0;
  this->block_received_ = 0;
  this->package_header_valid_ = false;
  this->bootinfo_valid_ = false;
}

}  // namespace esphome::ota
#endif

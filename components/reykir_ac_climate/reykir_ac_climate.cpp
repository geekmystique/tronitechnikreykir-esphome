#include "reykir_ac_climate.h"
#include "esphome/core/log.h"

namespace esphome {
namespace reykir_ac_climate {

static const char *const TAG = "reykir_ac_climate";

// ---------- helpers ----------

uint8_t ReykirAcClimate::checksum_(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += data[i];
  return (uint8_t) (sum & 0xFF);
}

// ---------- lifecycle ----------

void ReykirAcClimate::setup() {
  // Nothing special needed; first poll happens on the first update() call.
}

void ReykirAcClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "Reykir AC Climate (Tuya module replacement):");
  ESP_LOGCONFIG(TAG, "  Poll interval configured via update_interval");
}

void ReykirAcClimate::update() {
  // Send the fixed poll request. The MCU only reports its state in response
  // to this - it never pushes updates on its own.
  this->write_array(POLL_REQUEST, sizeof(POLL_REQUEST));
}

// ---------- receive / parse ----------

void ReykirAcClimate::loop() {
  while (this->available()) {
    uint8_t c;
    this->read_byte(&c);

    if (this->rx_len_ == 0) {
      if (c != 0xAA) continue;  // wait for header
      this->rx_buffer_[this->rx_len_++] = c;
      this->last_rx_byte_time_ = millis();
      continue;
    }

    // Basic inter-byte timeout in case a frame gets cut off - resync on next 0xAA.
    if (millis() - this->last_rx_byte_time_ > 200) {
      this->rx_len_ = 0;
      if (c == 0xAA) {
        this->rx_buffer_[this->rx_len_++] = c;
        this->last_rx_byte_time_ = millis();
      }
      continue;
    }

    this->rx_buffer_[this->rx_len_++] = c;
    this->last_rx_byte_time_ = millis();

    if (this->rx_len_ >= FRAME_LEN) {
      // Full 22-byte frame collected.
      if (this->rx_buffer_[IDX_LENGTH] != 0x14) {
        ESP_LOGW(TAG, "Unexpected length byte 0x%02X, discarding frame", this->rx_buffer_[IDX_LENGTH]);
        this->rx_len_ = 0;
        continue;
      }
      uint8_t expected = checksum_(this->rx_buffer_, FRAME_LEN - 1);
      if (expected != this->rx_buffer_[IDX_CHECKSUM]) {
        ESP_LOGW(TAG, "Checksum mismatch (got 0x%02X, expected 0x%02X), discarding frame",
                 this->rx_buffer_[IDX_CHECKSUM], expected);
        this->rx_len_ = 0;
        continue;
      }

      uint8_t type = this->rx_buffer_[IDX_TYPE];
      if (type == FRAME_TYPE_STATUS || type == FRAME_TYPE_COMMAND) {
        this->handle_status_frame_(this->rx_buffer_);
      } else {
        ESP_LOGW(TAG, "Unknown frame type 0x%02X", type);
      }
      this->rx_len_ = 0;
    }
  }
}

void ReykirAcClimate::handle_status_frame_(const uint8_t *frame) {
  // Cache the full frame (minus checksum) so future set-commands can mirror
  // every field and only change the one being written, exactly like the
  // original app did.
  memcpy(this->last_state_, frame, FRAME_LEN - 1);
  this->have_state_ = true;

  bool power_on = frame[IDX_POWER] != 0x00;
  uint8_t mode_byte = frame[IDX_MODE];
  uint8_t fan_byte = frame[IDX_FAN];
  uint8_t vane_byte = frame[IDX_VANE];
  uint8_t features = frame[IDX_FEATURES];
  uint8_t target_temp = frame[IDX_TARGET_TEMP];
  uint8_t room_temp = frame[IDX_ROOM_TEMP];

  if (!power_on) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (mode_byte) {
      case MODE_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case MODE_DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      case MODE_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case MODE_FAN_ONLY:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case MODE_AUTO:
      default:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
    }
  }

  switch (fan_byte) {
    case FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case FAN_MEDIUM:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case FAN_AUTO:
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  this->swing_mode = (vane_byte == VANE_SWING) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;

  this->target_temperature = target_temp;
  this->current_temperature = room_temp;

  this->publish_state();
  this->publish_switch_states_();
  this->publish_vane_select_();

  ESP_LOGD(TAG, "AC status: power=%d mode=0x%02X fan=0x%02X vane=0x%02X features=0x%02X target=%d room=%d",
           power_on, mode_byte, fan_byte, vane_byte, features, target_temp, room_temp);
}

void ReykirAcClimate::publish_switch_states_() {
  if (!this->have_state_) return;
  uint8_t features = this->last_state_[IDX_FEATURES];
  if (this->sleep_switch_ != nullptr) this->sleep_switch_->publish_state(features & FEATURE_SLEEP);
  if (this->uvc_switch_ != nullptr) this->uvc_switch_->publish_state(features & FEATURE_UVC);
  if (this->mute_switch_ != nullptr) this->mute_switch_->publish_state(features & FEATURE_MUTE);
  if (this->display_switch_ != nullptr) this->display_switch_->publish_state(features & FEATURE_DISPLAY);
}

void ReykirAcClimate::publish_vane_select_() {
  if (!this->have_state_ || this->vane_select_ == nullptr) return;
  uint8_t vane = this->last_state_[IDX_VANE];
  if (vane > VANE_FIXED_MAX) {
    ESP_LOGW(TAG, "Unexpected vane byte 0x%02X, not updating select", vane);
    return;
  }
  auto options = this->vane_select_->traits.get_options();
  if (vane < options.size()) {
    this->vane_select_->publish_state(options[vane]);
  }
}

void ReykirAcClimate::set_vane_byte(uint8_t value) {
  if (value > VANE_FIXED_MAX) {
    ESP_LOGW(TAG, "Ignoring out-of-range vane value 0x%02X", value);
    return;
  }
  this->send_current_state_(IDX_VANE, value);
  this->publish_vane_select_();
}

// ---------- send ----------

void ReykirAcClimate::send_command_(uint8_t *frame) {
  // frame must already have bytes 0-20 populated (header/length/type set by caller).
  frame[IDX_CHECKSUM] = checksum_(frame, FRAME_LEN - 1);
  this->write_array(frame, FRAME_LEN);
}

void ReykirAcClimate::send_current_state_(uint8_t changed_idx, uint8_t new_value, uint8_t changed_idx2,
                                         uint8_t new_value2) {
  if (!this->have_state_) {
    ESP_LOGW(TAG, "No known AC state yet, ignoring command (wait for first poll response)");
    return;
  }

  uint8_t frame[FRAME_LEN];
  memcpy(frame, this->last_state_, FRAME_LEN - 1);
  frame[IDX_HEADER] = 0xAA;
  frame[IDX_LENGTH] = 0x14;
  frame[IDX_TYPE] = FRAME_TYPE_COMMAND;
  // Bytes 17-18 are unconfirmed in function; mirroring last known values
  // rather than zeroing them, to stay closest to observed app behavior.
  frame[changed_idx] = new_value;
  if (changed_idx2 != 0xFF) frame[changed_idx2] = new_value2;

  this->send_command_(frame);

  // Optimistically update our cached state so subsequent commands (and the
  // climate frontend) reflect the change immediately, without waiting for
  // the next poll cycle.
  frame[IDX_TYPE] = this->last_state_[IDX_TYPE];  // don't let cache remember "command" as type
  memcpy(this->last_state_, frame, FRAME_LEN - 1);
}

void ReykirAcClimate::set_feature_bit(uint8_t bit, bool on) {
  if (!this->have_state_) {
    ESP_LOGW(TAG, "No known AC state yet, ignoring feature toggle (wait for first poll response)");
    return;
  }
  uint8_t features = this->last_state_[IDX_FEATURES];
  if (on) {
    features |= bit;
  } else {
    features &= ~bit;
  }
  this->send_current_state_(IDX_FEATURES, features);

  // Mute has a confirmed side effect: enabling it force-sets fan speed to
  // Low on the real hardware, and does not revert on disable. We don't
  // fake that locally - the next poll will report the AC's real fan speed
  // and update the frontend accordingly.
  this->publish_switch_states_();
}

// ---------- climate interface ----------

climate::ClimateTraits ReykirAcClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  // Single-setpoint device: simply not setting CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE
  // is sufficient - there is no "supports two point" flag to enable separately.
  traits.set_visual_min_temperature(16);
  traits.set_visual_max_temperature(31);
  traits.set_visual_temperature_step(1);

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT_COOL,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });

  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });

  // Standard climate swing_mode only exposes oscillating vs. a single fixed
  // position. Full control over all 5 fixed vane positions is available
  // separately via the vane_position select entity (see set_vane_byte()),
  // which writes the same byte[5] field with any 0x00-0x05 value.
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
  });

  return traits;
}

void ReykirAcClimate::control(const climate::ClimateCall &call) {
  if (!this->have_state_) {
    ESP_LOGW(TAG, "No known AC state yet, ignoring control call (wait for first poll response)");
    return;
  }

  // Power / mode - the AC has independent power and mode bytes, so turning
  // "on" into a specific mode may need up to two commands, matching how the
  // original app behaved (one field change per command).
  if (call.get_mode().has_value()) {
    climate::ClimateMode requested = *call.get_mode();

    if (requested == climate::CLIMATE_MODE_OFF) {
      if (this->last_state_[IDX_POWER] != 0x00) {
        this->send_current_state_(IDX_POWER, 0x00);
      }
    } else {
      uint8_t mode_byte;
      switch (requested) {
        case climate::CLIMATE_MODE_COOL:
          mode_byte = MODE_COOL;
          break;
        case climate::CLIMATE_MODE_HEAT:
          mode_byte = MODE_HEAT;
          break;
        case climate::CLIMATE_MODE_DRY:
          mode_byte = MODE_DRY;
          break;
        case climate::CLIMATE_MODE_FAN_ONLY:
          mode_byte = MODE_FAN_ONLY;
          break;
        case climate::CLIMATE_MODE_HEAT_COOL:
        default:
          mode_byte = MODE_AUTO;
          break;
      }

      if (this->last_state_[IDX_POWER] == 0x00) {
        this->send_current_state_(IDX_POWER, 0x01);
      }
      if (this->last_state_[IDX_MODE] != mode_byte) {
        this->send_current_state_(IDX_MODE, mode_byte);
      }
    }
  }

  if (call.get_target_temperature().has_value()) {
    uint8_t temp = (uint8_t) roundf(*call.get_target_temperature());
    this->send_current_state_(IDX_TARGET_TEMP, temp);
  }

  if (call.get_fan_mode().has_value()) {
    uint8_t fan_byte;
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:
        fan_byte = FAN_LOW;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        fan_byte = FAN_MEDIUM;
        break;
      case climate::CLIMATE_FAN_HIGH:
        fan_byte = FAN_HIGH;
        break;
      case climate::CLIMATE_FAN_AUTO:
      default:
        fan_byte = FAN_AUTO;
        break;
    }
    this->send_current_state_(IDX_FAN, fan_byte);
  }

  if (call.get_swing_mode().has_value()) {
    uint8_t vane_byte = (*call.get_swing_mode() == climate::CLIMATE_SWING_OFF) ? 0x01 : VANE_SWING;
    this->send_current_state_(IDX_VANE, vane_byte);
  }

  this->publish_state();
}

}  // namespace reykir_ac_climate
}  // namespace esphome

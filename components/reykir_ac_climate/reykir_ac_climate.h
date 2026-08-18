#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"

namespace esphome {
namespace reykir_ac_climate {

// Frame layout (all frames except the 4-byte poll request are 22 bytes):
//  0      header           0xAA
//  1      length           0x14 (fixed)
//  2      frame type       0x01 poll response | 0x02 set command / ack
//  3      power            0x00 off | 0x01 on
//  4      unused           always 0x00
//  5      vane/swing       0x00 swing | 0x01-0x05 fixed position
//  6      unused/static    always 0x09 (unknown function, always sent as-is)
//  7      mode             0x00 Auto, 0x01 Cool, 0x02 Dry, 0x03 Heat, 0x04 Fan Only
//  8      feature bitmask  bit 0x01 sleep, 0x02 uvc, 0x04 mute, 0x10 display
//  9      fan speed        0x00 Auto, 0x01 Low, 0x02 Medium, 0x03 High
//  10     target temp      direct degC
//  11     room temp        direct degC
//  12-16  padding          0x00
//  17-18  unknown          command-frames only, safe to send as 0x00 0x00
//  19-20  padding          0x00
//  21     checksum         sum of bytes 0-20 mod 256

static const uint8_t FRAME_LEN = 22;
static const uint8_t POLL_REQUEST[4] = {0xAA, 0x02, 0x01, 0xAD};

static const uint8_t IDX_HEADER = 0;
static const uint8_t IDX_LENGTH = 1;
static const uint8_t IDX_TYPE = 2;
static const uint8_t IDX_POWER = 3;
static const uint8_t IDX_VANE = 5;
static const uint8_t IDX_STATIC6 = 6;
static const uint8_t IDX_MODE = 7;
static const uint8_t IDX_FEATURES = 8;
static const uint8_t IDX_FAN = 9;
static const uint8_t IDX_TARGET_TEMP = 10;
static const uint8_t IDX_ROOM_TEMP = 11;
static const uint8_t IDX_CHECKSUM = 21;

static const uint8_t FRAME_TYPE_STATUS = 0x01;
static const uint8_t FRAME_TYPE_COMMAND = 0x02;

static const uint8_t MODE_AUTO = 0x00;
static const uint8_t MODE_COOL = 0x01;
static const uint8_t MODE_DRY = 0x02;
static const uint8_t MODE_HEAT = 0x03;
static const uint8_t MODE_FAN_ONLY = 0x04;

static const uint8_t FAN_AUTO = 0x00;
static const uint8_t FAN_LOW = 0x01;
static const uint8_t FAN_MEDIUM = 0x02;
static const uint8_t FAN_HIGH = 0x03;

static const uint8_t VANE_SWING = 0x00;
static const uint8_t VANE_FIXED_MIN = 0x01;
static const uint8_t VANE_FIXED_MAX = 0x05;

static const uint8_t FEATURE_SLEEP = 0x01;
static const uint8_t FEATURE_UVC = 0x02;
static const uint8_t FEATURE_MUTE = 0x04;
static const uint8_t FEATURE_DISPLAY = 0x10;

class ReykirAcSleepSwitch;
class ReykirAcUvcSwitch;
class ReykirAcMuteSwitch;
class ReykirAcDisplaySwitch;
class ReykirAcVaneSelect;

class ReykirAcClimate : public climate::Climate, public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;  // sends the poll request on the configured interval
  void dump_config() override;

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void set_sleep_switch(ReykirAcSleepSwitch *sw) { sleep_switch_ = sw; }
  void set_uvc_switch(ReykirAcUvcSwitch *sw) { uvc_switch_ = sw; }
  void set_mute_switch(ReykirAcMuteSwitch *sw) { mute_switch_ = sw; }
  void set_display_switch(ReykirAcDisplaySwitch *sw) { display_switch_ = sw; }
  void set_vane_select(ReykirAcVaneSelect *sel) { vane_select_ = sel; }

  // Called by the feature switches to flip one bit in byte[8] and send it.
  void set_feature_bit(uint8_t bit, bool on);

  // Called by the vane select to write byte[5] directly (0x00 swing, 0x01-0x05 fixed positions).
  void set_vane_byte(uint8_t value);

 protected:
  // Receive buffer / framing
  uint8_t rx_buffer_[FRAME_LEN];
  uint8_t rx_len_{0};
  uint32_t last_rx_byte_time_{0};

  // Last known full state frame (bytes 0-20, no checksum) so set-commands
  // can mirror all fields and change only the one being written, matching
  // the original app's behavior.
  uint8_t last_state_[FRAME_LEN];
  bool have_state_{false};

  ReykirAcSleepSwitch *sleep_switch_{nullptr};
  ReykirAcUvcSwitch *uvc_switch_{nullptr};
  ReykirAcMuteSwitch *mute_switch_{nullptr};
  ReykirAcDisplaySwitch *display_switch_{nullptr};
  ReykirAcVaneSelect *vane_select_{nullptr};

  static uint8_t checksum_(const uint8_t *data, uint8_t len);
  void handle_status_frame_(const uint8_t *frame);
  void send_command_(uint8_t *frame);  // frame must have bytes 0-20 set; fills header/len/type/checksum
  void send_current_state_(uint8_t changed_idx, uint8_t new_value, uint8_t changed_idx2 = 0xFF,
                            uint8_t new_value2 = 0);
  void publish_switch_states_();
  void publish_vane_select_();
};

// Thin switch wrappers so each feature bit shows up as its own HA entity.
// All four just flip a bit in byte[8] via the parent climate component.

class ReykirAcSleepSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ReykirAcClimate *parent) { parent_ = parent; }
  void write_state(bool state) override { parent_->set_feature_bit(FEATURE_SLEEP, state); }

 protected:
  ReykirAcClimate *parent_;
};

class ReykirAcUvcSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ReykirAcClimate *parent) { parent_ = parent; }
  void write_state(bool state) override { parent_->set_feature_bit(FEATURE_UVC, state); }

 protected:
  ReykirAcClimate *parent_;
};

class ReykirAcMuteSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ReykirAcClimate *parent) { parent_ = parent; }
  void write_state(bool state) override { parent_->set_feature_bit(FEATURE_MUTE, state); }

 protected:
  ReykirAcClimate *parent_;
};

class ReykirAcDisplaySwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ReykirAcClimate *parent) { parent_ = parent; }
  void write_state(bool state) override { parent_->set_feature_bit(FEATURE_DISPLAY, state); }

 protected:
  ReykirAcClimate *parent_;
};

// Exposes the full 6-way vane setting (swing + 5 fixed positions) that the
// standard climate swing_mode can't represent. Options list index maps
// directly to the byte value (index 0 = "Swing" = 0x00, index 5 = "Position
// 5 (Top)" = 0x05) - see VANE_OPTIONS in climate.py, keep both in sync.
class ReykirAcVaneSelect : public select::Select, public Component {
 public:
  void set_parent(ReykirAcClimate *parent) { parent_ = parent; }

 protected:
  void control(const std::string &value) override {
    auto idx = this->index_of(value);
    if (!idx.has_value()) return;
    parent_->set_vane_byte((uint8_t) idx.value());
  }

  ReykirAcClimate *parent_;
};

}  // namespace reykir_ac_climate
}  // namespace esphome

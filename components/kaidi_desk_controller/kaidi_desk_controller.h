#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"

#include <cstdint>
#include <vector>

namespace esphome {
namespace kaidi_desk_controller {

// Desk Controller V3 for a Kaidi KDDY094 standing desk. See docs/hardware.md and docs/protocol.md.
//
// Frame format (both directions): 68 01 <b2> <b3> <b4> <sum> 16, where sum = 01 + b2 + b3 + b4.
//   desk UART:    TX sends handset-style commands to the box on T7; RX reads the box's height frames on T8.
//   handset UART: RX only, on the handset's side of the T7 switch.
//   wake pin:     high raises T10 to ~4.5 V while we use the desk (like an awake handset); low leaves it alone.
//   switch pin:   high opens the T7 switch, cutting the handset off from the box so its frames can't collide
//                 with ours. If the ESP resets, the switch closes again and the handset just works.
//   sense pin:    T10 through a divider. High while we aren't driving T10 = the handset is awake.
class KaidiDeskController : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_desk_uart(uart::UARTComponent *uart) { this->desk_uart_ = uart; }
  void set_handset_uart(uart::UARTComponent *uart) { this->handset_uart_ = uart; }
  void set_wake_pin(GPIOPin *pin) { this->wake_pin_ = pin; }
  void set_switch_pin(GPIOPin *pin) { this->switch_pin_ = pin; }
  void set_sense_pin(GPIOPin *pin) { this->sense_pin_ = pin; }

  void set_height_sensor(sensor::Sensor *s) { this->height_sensor_ = s; }
  void set_raw_position_sensor(sensor::Sensor *s) { this->raw_position_sensor_ = s; }
  void set_status_sensor(sensor::Sensor *s) { this->status_sensor_ = s; }
  void set_collision_sensitivity_sensor(sensor::Sensor *s) { this->collision_sensitivity_sensor_ = s; }
  void set_last_move_result_text_sensor(text_sensor::TextSensor *s) { this->last_move_result_ = s; }
  void set_handset_button_text_sensor(text_sensor::TextSensor *s) { this->handset_button_ = s; }
  void set_moving_binary_sensor(binary_sensor::BinarySensor *s) { this->moving_sensor_ = s; }
  void set_handset_in_use_binary_sensor(binary_sensor::BinarySensor *s) { this->handset_in_use_sensor_ = s; }
  void set_handset_not_responding_binary_sensor(binary_sensor::BinarySensor *s) {
    this->handset_not_responding_sensor_ = s;
  }

  // dir: 1 up, 2 down, 6-9 go to preset 1-4 (the frame command we send)
  void move(int dir);
  // raw: mm above the desk's lowest position
  void move_to(int raw);
  void stop();

 protected:
  void read_desk_();
  void read_handset_();
  void publish_height_();
  void send_();
  void publish_result_(const char *why);
  void save_();
  int mm_(int raw) const;

  uart::UARTComponent *desk_uart_{nullptr};
  uart::UARTComponent *handset_uart_{nullptr};
  GPIOPin *wake_pin_{nullptr};
  GPIOPin *switch_pin_{nullptr};
  GPIOPin *sense_pin_{nullptr};

  sensor::Sensor *height_sensor_{nullptr};
  sensor::Sensor *raw_position_sensor_{nullptr};
  sensor::Sensor *status_sensor_{nullptr};
  sensor::Sensor *collision_sensitivity_sensor_{nullptr};
  text_sensor::TextSensor *last_move_result_{nullptr};
  text_sensor::TextSensor *handset_button_{nullptr};
  binary_sensor::BinarySensor *moving_sensor_{nullptr};
  binary_sensor::BinarySensor *handset_in_use_sensor_{nullptr};
  binary_sensor::BinarySensor *handset_not_responding_sensor_{nullptr};

  // Kept across restarts.
  struct Saved {
    // Height limits (raw), learned from end-of-travel stops and the handset's limit settings. Only used to
    // tell "already at the top/bottom" from a fault; they never restrict movement.
    int16_t min_limit;
    int16_t max_limit;
    // How long the desk keeps travelling after we let go, in ms. coast distance = speed x this time.
    int16_t coast_up_ms;
    int16_t coast_down_ms;
    // anti-collision level G-1..G-4 last seen in the handset's sensitivity menu (0 = never seen)
    int16_t sens_level;
  };
  Saved saved_{0, 440, 300, 300, 0};
  ESPPreferenceObject pref_;

  std::vector<uint8_t> frame_buf_;
  std::vector<uint8_t> handset_buf_;

  // latest decoded position: mm above the lowest position, -1 = unknown
  int cur_raw_{-1};
  int last_decoded_{-1};
  uint32_t last_height_ms_{0};
  // when the height last changed (0 = not recently), so "Desk moving" also shows handset moves
  uint32_t height_changed_ms_{0};
  int published_raw_{-1};
  // byte 2 of the box's frames: 0 normally, 48 (0x30) while locked out (E04)
  int box_status_{-1};
  // speed in mm/s (+ up, - down), measured over the last ~0.2 s of height frames
  float speed_mms_{0};
  int ring_pos_[8]{};
  uint32_t ring_ms_[8]{};
  uint8_t ring_i_{0};

  // whether we are driving T10 high and have the T7 switch open
  bool t10_driven_{false};
  // true when a move starts while the box is already awake (no wake tap needed)
  bool quick_start_{false};
  // true while the start-up read is running: the box is woken just to report its height
  bool wake_only_{false};
  bool boot_checked_{false};
  // 0 idle / 1 up / 2 down / 6-9 go to preset 1-4
  int move_dir_{0};
  uint32_t move_start_{0};
  // the short run of idle frames after a move: when it started and how long it lasts (0 = not running)
  uint32_t tail_start_{0};
  uint32_t tail_ms_{0};
  // when we started sending the command, and the position at that moment (-2 = not started)
  uint32_t press_since_{0};
  int press_raw_{-2};
  // furthest position reached in the commanded direction (to spot the handset reversing the desk)
  int move_peak_{-1};
  // last height seen during a move and when it last changed (to spot the desk stopping at its limit)
  int stall_pos_{-1};
  uint32_t stall_since_{0};
  // "Target height" moves: the height we're going to (raw), -1 = not a target move
  int target_raw_{-1};
  // set when a target move lets go, cleared once the coast has been measured
  int learn_dir_{0};
  int learn_pos_{0};
  float learn_speed_{0};

  // handset
  uint32_t handset_frame_ms_{0};
  int last_handset_cmd_{-1};
  bool first_run_{true};

  // binary sensor filters
  bool states_published_{false};
  bool moving_state_{false};
  bool in_use_state_{false};
  uint32_t in_use_since_{0};
  bool in_use_raw_{false};
  bool silent_state_{false};
  uint32_t silent_since_{0};
  bool silent_raw_{false};

  char why_buf_[72]{};
};

class DeskButton : public button::Button {
 public:
  void set_parent(KaidiDeskController *parent) { this->parent_ = parent; }
  // 0 stop, 1 up, 2 down, 6-9 preset 1-4
  void set_action(uint8_t action) { this->action_ = action; }

 protected:
  void press_action() override;
  KaidiDeskController *parent_{nullptr};
  uint8_t action_{0};
};

class DeskTargetNumber : public number::Number {
 public:
  void set_parent(KaidiDeskController *parent) { this->parent_ = parent; }

 protected:
  void control(float value) override;
  KaidiDeskController *parent_{nullptr};
};

}  // namespace kaidi_desk_controller
}  // namespace esphome

#include "kaidi_desk_controller.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace esphome {
namespace kaidi_desk_controller {

static const char *const TAG = "desk";
static const char *const FIRMWARE_VERSION = "2.1";

// The box reports mm above its lowest position; the desk height is that + 711 mm.
static const int MIN_HEIGHT_MM = 711;
// Largest position the decoder accepts (a little above the 440 mm stroke of the tested legs).
static const int MAX_RAW = 460;
static const int DEFAULT_MAX_LIMIT = 440;

int KaidiDeskController::mm_(int raw) const { return raw < 0 ? -1 : raw + MIN_HEIGHT_MM; }

void KaidiDeskController::setup() {
  // Both outputs low: T10 left alone, T7 switch closed (handset joined to the box).
  this->wake_pin_->setup();
  this->wake_pin_->digital_write(false);
  this->switch_pin_->setup();
  this->switch_pin_->digital_write(false);
  this->sense_pin_->setup();

  this->pref_ = global_preferences->make_preference<Saved>(fnv1_hash("kaidi_desk_controller_saved_v1"));
  Saved restored;
  if (this->pref_.load(&restored))
    this->saved_ = restored;
  // values kept across restarts: go back to the defaults if they're out of range
  if (this->saved_.coast_up_ms < 100 || this->saved_.coast_up_ms > 700)
    this->saved_.coast_up_ms = 300;
  if (this->saved_.coast_down_ms < 100 || this->saved_.coast_down_ms > 700)
    this->saved_.coast_down_ms = 300;
  if (this->saved_.min_limit < 0 || this->saved_.max_limit > MAX_RAW || this->saved_.min_limit >= this->saved_.max_limit) {
    this->saved_.min_limit = 0;
    this->saved_.max_limit = DEFAULT_MAX_LIMIT;
  }
  if (this->saved_.sens_level < 0 || this->saved_.sens_level > 4)
    this->saved_.sens_level = 0;

  // Same timing as the handset: it sends a frame every ~14 ms while awake.
  this->set_interval("rx", 10, [this]() {
    this->read_desk_();
    this->read_handset_();
  });
  this->set_interval("publish", 250, [this]() { this->publish_height_(); });
  this->set_interval("tx", 13, [this]() { this->send_(); });

  ESP_LOGI(TAG, "desk controller firmware %s", FIRMWARE_VERSION);
}

void KaidiDeskController::dump_config() {
  ESP_LOGCONFIG(TAG, "Kaidi Desk Controller:");
  ESP_LOGCONFIG(TAG, "  Firmware: %s", FIRMWARE_VERSION);
  LOG_PIN("  Wake (T10 drive) Pin: ", this->wake_pin_);
  LOG_PIN("  Handset Switch (T7) Pin: ", this->switch_pin_);
  LOG_PIN("  Awake Sense (T10) Pin: ", this->sense_pin_);
  ESP_LOGCONFIG(TAG, "  Coast time: up %d ms, down %d ms", this->saved_.coast_up_ms, this->saved_.coast_down_ms);
  ESP_LOGCONFIG(TAG, "  Learned limits: %d to %d mm", this->mm_(this->saved_.min_limit),
                this->mm_(this->saved_.max_limit));
}

void KaidiDeskController::save_() { this->pref_.save(&this->saved_); }

// ---------------------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------------------

void KaidiDeskController::move(int dir) {
  // no wake tap if the box is already awake: we're holding T10, or the handset is (a tap into an awake box
  // would twitch the desk)
  this->quick_start_ = this->t10_driven_ || this->sense_pin_->digital_read();
  this->move_dir_ = dir;
  this->wake_only_ = false;
  this->target_raw_ = -1;
  this->learn_dir_ = 0;
  this->move_start_ = millis();
  this->press_raw_ = -2;
  this->stall_pos_ = -1;
  this->stall_since_ = millis();
  const char *awake = this->quick_start_ ? " (box already awake)" : "";
  if (dir >= 6) {
    ESP_LOGI(TAG, "move to preset %d%s", dir - 5, awake);
  } else {
    ESP_LOGI(TAG, "move %s%s", dir == 1 ? "up" : "down", awake);
  }
}

// The direction is chosen from the last known height here, and checked again once the box is awake and
// reporting, just before the command starts.
void KaidiDeskController::move_to(int raw) {
  int t = raw < 0 ? 0 : (raw > MAX_RAW ? MAX_RAW : raw);
  this->quick_start_ = this->t10_driven_ || this->sense_pin_->digital_read();
  this->target_raw_ = t;
  this->wake_only_ = false;
  this->learn_dir_ = 0;
  this->move_dir_ = (this->cur_raw_ >= 0 && t < this->cur_raw_) ? 2 : 1;
  this->move_start_ = millis();
  this->press_raw_ = -2;
  this->stall_pos_ = -1;
  this->stall_since_ = millis();
  ESP_LOGI(TAG, "move to %d mm%s", this->mm_(t), this->quick_start_ ? " (box already awake)" : "");
}

void KaidiDeskController::stop() {
  if (this->move_dir_ != 0) {
    this->move_dir_ = 0;
    this->tail_start_ = millis();
    this->tail_ms_ = 500;
    this->publish_result_("stopped");
  }
  ESP_LOGI(TAG, "stop at %d mm", this->mm_(this->cur_raw_));
}

// Publishes why a move ended. The start-up read isn't a move, so it ends without a result.
void KaidiDeskController::publish_result_(const char *why) {
  if (!this->wake_only_ && this->last_move_result_ != nullptr)
    this->last_move_result_->publish_state(why);
  this->wake_only_ = false;
}

// ---------------------------------------------------------------------------------------------------------
// Height frames from the box
// ---------------------------------------------------------------------------------------------------------

// The box sends `68 01 00 HI LO SUM 16` on T8 every ~25 ms while awake (live during movement) and for ~12 s
// after the handset sleeps. Junk bytes can sit between frame bytes, so on each 0x16 we look back to the last
// "68 01" and pick out 4 bytes in order that form a valid frame:
//   <status> <hi> <lo> <sum>, with sum = 01 + status + hi + lo and the value within desk travel.
// A value is accepted when it's within 10 of the previous decode (the desk moves ~1-2 per frame).
void KaidiDeskController::read_desk_() {
  auto &buf = this->frame_buf_;
  uint8_t b;
  while (this->desk_uart_->available()) {
    this->desk_uart_->read_byte(&b);
    buf.push_back(b);
    if (buf.size() > 20)
      buf.erase(buf.begin());
    if (b != 0x16)
      continue;

    int end = buf.size() - 1;
    int start = -1;
    for (int i = end - 2; i >= 0; i--) {
      if (buf[i] == 0x68 && buf[i + 1] == 0x01) {
        start = i + 2;
        break;
      }
    }
    if (start < 0)
      continue;
    // Fewer than 4 bytes since "68 01": this 0x16 is a data byte, not the frame end (height 21/22 or 276/278 mm
    // above minimum makes LO or SUM = 0x16). Wait for the real end instead of counting it as bad.
    if (end - start < 4)
      continue;

    int found = -1, status = -1;
    bool ambiguous = false;
    for (int a = start; a < end; a++) {
      for (int h = a + 1; h < end; h++) {
        if (buf[h] > 0x01)
          continue;
        for (int l = h + 1; l < end; l++) {
          int value = (buf[h] << 8) | buf[l];
          if (value > MAX_RAW)
            continue;
          uint8_t sum = 0x01 + buf[a] + buf[h] + buf[l];
          for (int c = l + 1; c < end; c++) {
            if (buf[c] != sum)
              continue;
            if (found >= 0 && (found != value || status != buf[a]))
              ambiguous = true;
            found = value;
            status = buf[a];
          }
        }
      }
    }
    if (found < 0 || ambiguous)
      continue;  // couldn't decode this frame: wait for the next one
    buf.clear();

    if (status != this->box_status_) {
      this->box_status_ = status;
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state(status);
    }
    // While the handset is in a settings menu the box sends other statuses (e.g. 0x62 = 98) with the setting's
    // value where the height normally is. Only 0 (normal) and 0x30 (E04) frames carry a height.
    if (status == 0x62 && found >= 1 && found <= 4 && found != this->saved_.sens_level) {
      this->saved_.sens_level = found;  // sensitivity menu: current level G-1..G-4
      this->save_();
      if (this->collision_sensitivity_sensor_ != nullptr)
        this->collision_sensitivity_sensor_->publish_state(found);
    }
    if (status != 0 && status != 0x30)
      continue;

    int last = this->last_decoded_;
    this->last_decoded_ = found;
    if (last < 0 || abs(found - last) > 10)
      continue;
    uint32_t now = millis();
    if (this->cur_raw_ >= 0 && found != this->cur_raw_)
      this->height_changed_ms_ = now ? now : 1;
    this->cur_raw_ = found;
    this->last_height_ms_ = now;

    // speed over the last 8 accepted frames (~0.2 s); 0 if that slot is empty or too old to compare
    uint8_t i = this->ring_i_;
    uint32_t dt = now - this->ring_ms_[i];
    this->speed_mms_ = (this->ring_ms_[i] != 0 && dt >= 100 && dt <= 600)
                           ? (found - this->ring_pos_[i]) * 1000.0f / dt
                           : 0.0f;
    this->ring_pos_[i] = found;
    this->ring_ms_[i] = now;
    this->ring_i_ = (i + 1) & 7;
  }
}

// ---------------------------------------------------------------------------------------------------------
// Frames from the handset
// ---------------------------------------------------------------------------------------------------------

// Frames: 68 01 <cmd> b3 b4 sum 16. cmd 03 idle, 01 up, 02 down, 06/07/08/09 = memory preset 1-4. On the tested
// desk b3/b4 were 0 and the box drove to its own stored height. S alone sends nothing; S then a number sends
// one "save" frame: S+1/2/3 = 0x0A/0B/0C, S+4 = 0x17. Others are reported as "other (cmd N)".
// - Always: report which handset button is in use.
// - While we're moving the desk (T7 switch open): any button ends our move and joins the handset back to the
//   box, so a handset press always wins.
void KaidiDeskController::read_handset_() {
  if (this->first_run_) {
    // just started: no handset frame yet means no button is pressed, and the anti-collision level is
    // whatever the sensitivity menu last showed
    this->first_run_ = false;
    if (this->handset_button_ != nullptr)
      this->handset_button_->publish_state("idle");
    if (this->saved_.sens_level >= 1 && this->saved_.sens_level <= 4 && this->collision_sensitivity_sensor_ != nullptr)
      this->collision_sensitivity_sensor_->publish_state(this->saved_.sens_level);
  }

  auto &buf = this->handset_buf_;
  uint8_t b;
  while (this->handset_uart_->available()) {
    this->handset_uart_->read_byte(&b);
    buf.push_back(b);
    if (buf.size() > 7)
      buf.erase(buf.begin());
    if (buf.size() < 7 || buf[0] != 0x68 || buf[1] != 0x01 || buf[6] != 0x16)
      continue;
    if (buf[5] != (uint8_t) (0x01 + buf[2] + buf[3] + buf[4]))
      continue;
    this->handset_frame_ms_ = millis();
    int cmd = buf[2];
    int b3 = buf[3], b4 = buf[4];
    int preset_mm = b3 | (b4 << 8);
    buf.clear();
    if (cmd != 0x03)
      this->learn_dir_ = 0;  // the handset is moving the desk: don't measure a coast from this

    if (cmd != this->last_handset_cmd_) {
      this->last_handset_cmd_ = cmd;
      int h = this->mm_(this->cur_raw_);
      char what[48];
      if (cmd == 0x03) {
        snprintf(what, sizeof(what), "idle");
      } else if (cmd == 0x01) {
        snprintf(what, sizeof(what), "up");
      } else if (cmd == 0x02) {
        snprintf(what, sizeof(what), "down");
      } else if (cmd >= 0x06 && cmd <= 0x09) {
        if (preset_mm > 0) {
          snprintf(what, sizeof(what), "preset %d (%d mm)", cmd - 5, preset_mm);
        } else {
          snprintf(what, sizeof(what), "preset %d", cmd - 5);
        }
      } else if ((cmd >= 0x0A && cmd <= 0x0C) || cmd == 0x17) {
        // S then a preset number: save the current height
        snprintf(what, sizeof(what), "save preset %d (%d mm)", cmd == 0x17 ? 4 : cmd - 0x09, h);
      } else if (cmd == 0x0D) {
        // hold S, then hold 1 until -L-: lower limit set at the current height
        snprintf(what, sizeof(what), "set lower limit (%d mm)", h);
        if (this->cur_raw_ >= 0) {
          this->saved_.min_limit = this->cur_raw_;
          this->save_();
        }
      } else if (cmd == 0x13) {
        // hold S, then hold 3 until -L-: upper limit set at the current height
        snprintf(what, sizeof(what), "set upper limit (%d mm)", h);
        if (this->cur_raw_ >= 0) {
          this->saved_.max_limit = this->cur_raw_;
          this->save_();
        }
      } else if (cmd == 0x0E) {
        // hold S, then hold 2 until -C-: limits cleared (seen as 0x14 then 0x0E)
        snprintf(what, sizeof(what), "clear limits");
        this->saved_.min_limit = 0;
        this->saved_.max_limit = DEFAULT_MAX_LIMIT;
        this->save_();
      } else if (cmd == 0x62) {
        snprintf(what, sizeof(what), "sensitivity menu");
      } else if (cmd == 0x19) {
        snprintf(what, sizeof(what), "set sensitivity G-%d", b4);
        if (b4 >= 1 && b4 <= 4 && b4 != this->saved_.sens_level) {
          this->saved_.sens_level = b4;
          this->save_();
          if (this->collision_sensitivity_sensor_ != nullptr)
            this->collision_sensitivity_sensor_->publish_state(b4);
        }
      } else {
        snprintf(what, sizeof(what), "other (cmd %d, %d %d)", cmd, b3, b4);
      }
      if (this->cur_raw_ < 0) {
        ESP_LOGI(TAG, "handset: %s (height not known yet)", what);
      } else {
        ESP_LOGI(TAG, "handset: %s at %d mm", what, h);
      }
      if (this->handset_button_ != nullptr)
        this->handset_button_->publish_state(what);
    }

    if (this->t10_driven_ && cmd != 0x03) {
      ESP_LOGI(TAG, "handset button (cmd %d): handing back at %d mm", cmd, this->mm_(this->cur_raw_));
      this->move_dir_ = 0;
      this->tail_ms_ = 0;
      this->wake_pin_->digital_write(false);
      this->switch_pin_->digital_write(false);
      this->t10_driven_ = false;
      this->publish_result_("handset took over");
    }
  }
}

// ---------------------------------------------------------------------------------------------------------
// Height to Home Assistant, at most 4 times a second (it changes every frame while moving)
// ---------------------------------------------------------------------------------------------------------

void KaidiDeskController::publish_height_() {
  // After a target move: once the desk has settled, measure how far it coasted and adjust the coast time.
  if (this->learn_dir_ != 0 && this->move_dir_ == 0 && this->cur_raw_ >= 0 &&
      (this->height_changed_ms_ == 0 || millis() - this->height_changed_ms_ >= 1000)) {
    int coast = abs(this->cur_raw_ - this->learn_pos_);
    ESP_LOGI(TAG, "target %d mm: settled at %d mm (coasted %d mm from %.0f mm/s)", this->mm_(this->target_raw_),
             this->mm_(this->cur_raw_), coast, this->learn_speed_);
    if (this->learn_speed_ >= 15.0f && coast <= 40) {
      int t = (int) (coast * 1000.0f / this->learn_speed_);
      t = t < 100 ? 100 : (t > 700 ? 700 : t);
      int16_t &stored = this->learn_dir_ == 1 ? this->saved_.coast_up_ms : this->saved_.coast_down_ms;
      int updated = (stored * 2 + t) / 3;
      if (abs(updated - stored) >= 10) {
        stored = updated;
        this->save_();
        ESP_LOGI(TAG, "coast time %s now %d ms", this->learn_dir_ == 1 ? "up" : "down", updated);
      }
    }
    this->learn_dir_ = 0;
  }

  int pos = this->cur_raw_;
  if (pos < 0 || pos == this->published_raw_)
    return;
  this->published_raw_ = pos;
  if (this->raw_position_sensor_ != nullptr)
    this->raw_position_sensor_->publish_state(pos);
  if (this->height_sensor_ != nullptr)
    this->height_sensor_->publish_state(this->mm_(pos));
}

// ---------------------------------------------------------------------------------------------------------
// Commands to the box
// ---------------------------------------------------------------------------------------------------------

// The T7 switch is open whenever we hold T10, so the handset can't collide with these frames.
// Handset frames go out on T7 every ~13 ms, copying what the handset does when it wakes a sleeping box:
//   0-50 ms idle, 50-150 ms a short "tap" of the direction (wakes the box and switches its relay on),
//   150-1650 ms idle, then 68 01 <cmd> 00 00 sum 16 until the move ends, then 0.5 s of idle frames so the box
//   sees the release, and then we go quiet so the handset can take over straight away. T10 is held high
//   throughout. If a new move starts while the box is already awake, the command starts after 200 ms, no tap.
// A move also ends if the desk reports an error, doesn't start moving, stops at its limit (height unchanged for
// 1.5 s after it has moved), stops reporting, or after 30 s.
// If the desk moves more than 5 mm the wrong way, someone is using the handset: stop at once and skip the idle
// frames, so we don't talk over the handset.
void KaidiDeskController::send_() {
  uint32_t now = millis();

  // Start-up read: 15 s after boot, once Wi-Fi has settled, so the two loads on the 5 V supply do not overlap.
  // If the box has not reported a height (it is asleep) and nobody is using the desk, wake it once so the
  // height and status are known. This is the usual wake sequence with no command after it, so the desk does
  // not move; the box clicks on, then goes back to sleep by itself.
  if (!this->boot_checked_ && now >= 15000) {
    this->boot_checked_ = true;
    if (this->cur_raw_ < 0 && this->move_dir_ == 0 && !this->sense_pin_->digital_read()) {
      this->wake_only_ = true;
      this->quick_start_ = false;
      this->target_raw_ = -1;
      this->learn_dir_ = 0;
      this->move_dir_ = 1;
      this->move_start_ = now;
      this->press_raw_ = -2;
      this->stall_pos_ = -1;
      this->stall_since_ = now;
      ESP_LOGI(TAG, "start-up: waking the box to read the height");
    }
  }

  // (times are compared as "now - start", which stays correct when millis() wraps after 49.7 days)
  if (this->tail_ms_ != 0 && now - this->tail_start_ >= this->tail_ms_)
    this->tail_ms_ = 0;
  bool want_t10 = this->move_dir_ != 0 || this->tail_ms_ != 0;
  if (want_t10 != this->t10_driven_) {
    this->switch_pin_->digital_write(want_t10);
    this->wake_pin_->digital_write(want_t10);
    this->t10_driven_ = want_t10;
  }

  uint8_t cmd = 0x03;
  int dir = this->move_dir_;
  if (dir != 0) {
    uint32_t elapsed = now - this->move_start_;
    bool fresh = this->cur_raw_ >= 0 && now - this->last_height_ms_ < 1000;
    const char *why = nullptr;
    bool handset = false;
    if (this->box_status_ == 0x30 && fresh) {
      why = "desk reports an error - reset it by holding UP+DOWN on the handset";
    } else if (this->box_status_ > 0 && fresh) {
      why = "desk isn't ready - check the handset's display";
    } else if (elapsed > 30000) {
      why = "timeout";
    } else if (elapsed > 5000 && !fresh) {
      why = "no height from the desk";
    }

    uint32_t start_at = this->quick_start_ ? 200 : 1650;
    if (!why && !this->quick_start_ && elapsed >= 50 && elapsed < 150) {
      cmd = dir;  // wake tap
    } else if (!why && elapsed >= start_at) {
      bool hold = false;
      if (this->wake_only_) {
        // start-up read: finished as soon as the box reports its height; never send a command
        if (fresh) {
          why = "height read at start-up";
        } else {
          hold = true;
        }
      }
      if (this->press_raw_ == -2 && this->target_raw_ >= 0) {
        // going to a height: wait until the box is reporting its height and the desk is standing still
        // (it may be coasting from a move we just interrupted), then choose the direction
        if (!fresh || fabsf(this->speed_mms_) > 3.0f) {
          hold = true;
        } else if (abs(this->target_raw_ - this->cur_raw_) <= 4) {
          snprintf(this->why_buf_, sizeof(this->why_buf_), "already at target (%d mm)", this->mm_(this->target_raw_));
          why = this->why_buf_;
        } else {
          dir = this->target_raw_ > this->cur_raw_ ? 1 : 2;
          this->move_dir_ = dir;
        }
      }
      if (!why && !hold && this->press_raw_ == -2) {
        this->press_since_ = now;
        this->press_raw_ = this->cur_raw_;
        this->move_peak_ = this->cur_raw_;
        if (this->target_raw_ >= 0) {
          ESP_LOGI(TAG, "sending %s towards %d mm from %d mm", dir == 1 ? "up" : "down", this->mm_(this->target_raw_),
                   this->mm_(this->cur_raw_));
        } else {
          ESP_LOGI(TAG, "sending %s %d from %d mm", dir >= 6 ? "preset" : (dir == 1 ? "up" : "down"),
                   dir >= 6 ? dir - 5 : 0, this->mm_(this->cur_raw_));
        }
      }

      int pos = this->cur_raw_;
      if (pos >= 0) {
        this->move_peak_ = this->move_peak_ < 0 ? pos
                                               : dir == 1 ? std::max(this->move_peak_, pos)
                                                          : std::min(this->move_peak_, pos);
      }
      float coast_ms = dir == 1 ? this->saved_.coast_up_ms : this->saved_.coast_down_ms;
      float coast_mm = fabsf(this->speed_mms_) * coast_ms / 1000.0f;

      // (presets can go either way, so the wrong-way check is for Up/Down only; the handset listener still
      // catches any handset button during a preset move)
      if (why || hold) {
        // nothing to send yet: still waiting for the height, or already there
      } else if (dir <= 2 && pos >= 0 && this->move_peak_ >= 0 &&
                 (dir == 1 ? pos < this->move_peak_ - 5 : pos > this->move_peak_ + 5)) {
        why = "desk moving the other way - handset in use";
        handset = true;
      } else if ((pos < 0 || abs(pos - this->press_raw_) < 5) && now - this->press_since_ > 2500) {
        // already at a limit, or our commands aren't reaching the box. With the T7 switch open we only reach
        // the box through the DESK jack, so swapped cables look exactly like this.
        if (dir >= 6) {
          snprintf(this->why_buf_, sizeof(this->why_buf_), "already at preset %d (or it isn't saved)", dir - 5);
          why = this->why_buf_;
        } else if (pos >= 0 && dir == 1 && pos >= this->saved_.max_limit - 5) {
          why = "already at the top";
        } else if (pos >= 0 && dir == 2 && pos <= this->saved_.min_limit + 5) {
          why = "already at the bottom";
        } else {
          why = "desk didn't respond - check the desk and handset cables aren't swapped";
        }
      } else if (pos >= 0 && abs(pos - this->press_raw_) >= 5 && pos == this->stall_pos_ &&
                 now - this->stall_since_ > 1500) {
        // it moved, then the height stopped changing: the box has hit its limit (or stopped itself)
        if (dir >= 6) {
          // a preset move: the box drove to its stored height and stopped by itself
          snprintf(this->why_buf_, sizeof(this->why_buf_), "reached preset %d (%d mm)", dir - 5, this->mm_(pos));
          why = this->why_buf_;
        } else {
          why = "desk stopped moving (end of travel)";
          if (dir == 1) {
            this->saved_.max_limit = pos;
          } else {
            this->saved_.min_limit = pos;
          }
          this->save_();
        }
      } else if (this->target_raw_ >= 0 && pos >= 0 &&
                 (dir == 1 ? pos + coast_mm >= this->target_raw_ : pos - coast_mm <= this->target_raw_)) {
        // close enough that the desk will coast the rest of the way: let go now
        snprintf(this->why_buf_, sizeof(this->why_buf_), "reached target (%d mm)", this->mm_(this->target_raw_));
        why = this->why_buf_;
        this->learn_dir_ = dir;
        this->learn_pos_ = pos;
        this->learn_speed_ = fabsf(this->speed_mms_);
      } else {
        cmd = dir;
      }
      if (pos != this->stall_pos_) {
        this->stall_pos_ = pos;
        this->stall_since_ = now;
      }
    }

    if (why) {
      this->move_dir_ = 0;
      this->tail_start_ = now;
      this->tail_ms_ = handset ? 0 : 500;
      if (handset) {
        // let go of the wire straight away; the handset is driving the desk now
        this->wake_pin_->digital_write(false);
        this->switch_pin_->digital_write(false);
        this->t10_driven_ = false;
        ESP_LOGI(TAG, "stop (%s) at %d mm", why, this->mm_(this->cur_raw_));
        this->publish_result_(why);
        return;
      }
      cmd = 0x03;
      ESP_LOGI(TAG, "stop (%s) at %d mm, status %d", why, this->mm_(this->cur_raw_), this->box_status_);
      this->publish_result_(why);
    }
  } else if (this->tail_ms_ == 0) {
    return;
  }

  uint8_t frame[7] = {0x68, 0x01, cmd, 0x00, 0x00, (uint8_t) (0x01 + cmd), 0x16};
  this->desk_uart_->write_array(frame, 7);
}

// ---------------------------------------------------------------------------------------------------------
// Binary sensors
// ---------------------------------------------------------------------------------------------------------

void KaidiDeskController::loop() {
  uint32_t now = millis();
  bool first = !this->states_published_;
  this->states_published_ = true;

  // Desk moving: on while we're running a move, or the height changed in the last 1.2 s (handset moves and
  // coasting). The start-up read doesn't count: it wakes the box without moving the desk.
  if (this->height_changed_ms_ != 0 && now - this->height_changed_ms_ >= 1200)
    this->height_changed_ms_ = 0;
  bool moving = (this->move_dir_ != 0 && !this->wake_only_) || this->height_changed_ms_ != 0;
  if (first || moving != this->moving_state_) {
    this->moving_state_ = moving;
    if (this->moving_sensor_ != nullptr)
      this->moving_sensor_->publish_state(moving);
  }

  // Handset in use: T10 is high and we aren't the ones driving it. Must hold for 50 ms before turning on,
  // which skips the moment T10 is still falling after we let go.
  bool awake = this->sense_pin_->digital_read() && !this->t10_driven_;
  if (awake != this->in_use_raw_) {
    this->in_use_raw_ = awake;
    this->in_use_since_ = now;
  }
  bool in_use = awake && (this->in_use_state_ || now - this->in_use_since_ >= 50);
  if (first || in_use != this->in_use_state_) {
    this->in_use_state_ = in_use;
    if (this->handset_in_use_sensor_ != nullptr)
      this->handset_in_use_sensor_->publish_state(in_use);
  }

  // Handset not responding: awake but silent. An awake handset sends a frame every ~14 ms, so 3 s without one,
  // held for another 2 s, means it has stopped responding.
  bool silent = awake && now - this->handset_frame_ms_ > 3000;
  if (silent != this->silent_raw_) {
    this->silent_raw_ = silent;
    this->silent_since_ = now;
  }
  bool not_responding = silent && (this->silent_state_ || now - this->silent_since_ >= 2000);
  if (first || not_responding != this->silent_state_) {
    this->silent_state_ = not_responding;
    if (not_responding)
      ESP_LOGW(TAG, "handset is awake but silent - unplug the handset and plug it back in");
    if (this->handset_not_responding_sensor_ != nullptr)
      this->handset_not_responding_sensor_->publish_state(not_responding);
  }
}

// ---------------------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------------------

void DeskButton::press_action() {
  if (this->parent_ == nullptr)
    return;
  if (this->action_ == 0) {
    this->parent_->stop();
  } else {
    this->parent_->move(this->action_);
  }
}

void DeskTargetNumber::control(float value) {
  this->publish_state(value);
  if (this->parent_ != nullptr)
    this->parent_->move_to((int) value - MIN_HEIGHT_MM);
}

}  // namespace kaidi_desk_controller
}  // namespace esphome

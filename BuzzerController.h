#pragma once
#include <Arduino.h>

// Audible feedback that tracks the actual press, not a fixed-length click.
// This was present in the legacy firmware (buz_pin/buz_pinnew, auto-off
// after ~300ms regardless of hold) but was dropped entirely in the
// rewrite — GPIO5 matches the legacy default pin and isn't used by any
// button/LED/RS485 variant in this firmware.
//
// Behavior: trigger() on PRESSED starts the buzzer immediately and keeps it
// on continuously while held — NOT a fixed-duration blip — so a long hold
// is audibly a long hold, not a click. It stops at whichever of these
// happens first:
//   - maxDurationMs_ elapses since trigger() (the long-press threshold is
//     reached — matches DebounceEngine's longPressMs, see the two drivers'
//     `debounce_.begin(50, 800)` calls; keep this in sync with those if you
//     ever change one), even if the button is still physically held. This
//     caps a long press at one bounded beep instead of buzzing indefinitely.
//   - release() is called (the RELEASED event arrives) AND at least
//     minDurationMs_ has elapsed since trigger() — the floor exists so a
//     very quick tap still produces an audible beep instead of an
//     instant on/off blip too short to hear.
#define BUZZER_PIN 5

class BuzzerController {
public:
  void begin(uint16_t minDurationMs = 100, uint16_t maxDurationMs = 800) {
    minDurationMs_ = minDurationMs;
    maxDurationMs_ = maxDurationMs;
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Call on ButtonEventType::PRESSED — starts beeping immediately.
  void trigger(uint32_t nowMs) {
    digitalWrite(BUZZER_PIN, HIGH);
    startMs_ = nowMs;
    releaseRequested_ = false;
    active_ = true;
  }

  // Call on ButtonEventType::RELEASED — requests the beep stop, subject to
  // the minDurationMs_ floor enforced in tick().
  void release() {
    releaseRequested_ = true;
  }

  void tick(uint32_t nowMs) {
    if (!active_) return;
    uint32_t elapsed = nowMs - startMs_;
    bool longPressCapReached = elapsed >= maxDurationMs_;
    bool releasedPastMinFloor = releaseRequested_ && elapsed >= minDurationMs_;
    if (longPressCapReached || releasedPastMinFloor) {
      digitalWrite(BUZZER_PIN, LOW);
      active_ = false;
    }
  }

private:
  uint16_t minDurationMs_ = 100;
  uint16_t maxDurationMs_ = 800;
  uint32_t startMs_ = 0;
  bool releaseRequested_ = false;
  bool active_ = false;
};

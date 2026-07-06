#pragma once
#include <Arduino.h>

#define DEBOUNCE_NO_KEY 0xFF

// One shared time-based debounce + press-duration primitive used by every
// IButtonInput implementation. Replaces the legacy firmware's two divergent
// debounce styles (a ~60-consecutive-poll hold-time counter on the raw
// shift-register byte, and a plain change-detector on the decoded logical
// value) with a single millis()-based implementation.
//
// The confirmed event fires on RELEASE (not on press) so press-duration —
// and therefore long-press vs short-press — is always known by the time the
// caller sees the event; no busy-wait loop is needed.
class DebounceEngine {
public:
  void begin(uint16_t stableMs = 50, uint16_t longPressMs = 800) {
    stableMs_ = stableMs;
    longPressMs_ = longPressMs;
    candidate_ = DEBOUNCE_NO_KEY;
    confirmed_ = DEBOUNCE_NO_KEY;
    candidateSinceMs_ = 0;
    pressStartMs_ = 0;
  }

  // rawKey: the currently-sampled logical key (DEBOUNCE_NO_KEY if none pressed).
  // Returns true exactly once, on the release of a debounced press, with
  // outKey set to the button that was released and outIsLongPress reflecting
  // hold duration.
  bool update(uint8_t rawKey, uint32_t nowMs, uint8_t &outKey, bool &outIsLongPress) {
    if (rawKey != candidate_) {
      candidate_ = rawKey;
      candidateSinceMs_ = nowMs;
      return false;
    }

    if ((uint32_t)(nowMs - candidateSinceMs_) < stableMs_) return false;
    if (candidate_ == confirmed_) return false;   // already-settled state, nothing new

    if (candidate_ != DEBOUNCE_NO_KEY) {
      // New stable press.
      confirmed_ = candidate_;
      pressStartMs_ = nowMs;
      return false;
    }

    // New stable release of whatever was previously confirmed as pressed.
    uint8_t releasedKey = confirmed_;
    confirmed_ = DEBOUNCE_NO_KEY;
    outKey = releasedKey;
    outIsLongPress = (uint32_t)(nowMs - pressStartMs_) >= longPressMs_;
    return true;
  }

private:
  uint16_t stableMs_ = 50;
  uint16_t longPressMs_ = 800;
  uint8_t  candidate_ = DEBOUNCE_NO_KEY;
  uint8_t  confirmed_ = DEBOUNCE_NO_KEY;
  uint32_t candidateSinceMs_ = 0;
  uint32_t pressStartMs_ = 0;
};

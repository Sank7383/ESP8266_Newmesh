#pragma once
#include <Arduino.h>

#define DEBOUNCE_NO_KEY 0xFF

enum class DebounceResult : uint8_t { NONE, PRESSED, RELEASED };

// One shared time-based debounce + press-duration primitive used by every
// IButtonInput implementation. Replaces the legacy firmware's two divergent
// debounce styles (a ~60-consecutive-poll hold-time counter on the raw
// shift-register byte, and a plain change-detector on the decoded logical
// value) with a single millis()-based implementation.
//
// Fires TWICE per button press: PRESSED the moment the press debounces
// stable (so callers can give instant feedback, e.g. a buzzer beep), and
// RELEASED when it's let go (with isLongPress known by then, since the full
// hold duration has elapsed) — the actual call-state action should only be
// applied on RELEASED, since long-press vs short-press isn't known until
// then.
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
  // outKey: on PRESSED, the key that just went down; on RELEASED, the key
  // that was just let go. outIsLongPress is only meaningful on RELEASED.
  DebounceResult update(uint8_t rawKey, uint32_t nowMs, uint8_t &outKey, bool &outIsLongPress) {
    if (rawKey != candidate_) {
      candidate_ = rawKey;
      candidateSinceMs_ = nowMs;
      return DebounceResult::NONE;
    }

    if ((uint32_t)(nowMs - candidateSinceMs_) < stableMs_) return DebounceResult::NONE;
    if (candidate_ == confirmed_) return DebounceResult::NONE;   // already-settled state, nothing new

    if (candidate_ != DEBOUNCE_NO_KEY) {
      // New stable press — fire immediately, before release, so the caller
      // can give instant feedback.
      confirmed_ = candidate_;
      pressStartMs_ = nowMs;
      outKey = confirmed_;
      return DebounceResult::PRESSED;
    }

    // New stable release of whatever was previously confirmed as pressed.
    uint8_t releasedKey = confirmed_;
    confirmed_ = DEBOUNCE_NO_KEY;
    outKey = releasedKey;
    outIsLongPress = (uint32_t)(nowMs - pressStartMs_) >= longPressMs_;
    return DebounceResult::RELEASED;
  }

private:
  uint16_t stableMs_ = 50;
  uint16_t longPressMs_ = 800;
  uint8_t  candidate_ = DEBOUNCE_NO_KEY;
  uint8_t  confirmed_ = DEBOUNCE_NO_KEY;
  uint32_t candidateSinceMs_ = 0;
  uint32_t pressStartMs_ = 0;
};

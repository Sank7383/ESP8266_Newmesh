#pragma once
#include <Arduino.h>

// millis()-based 2-color blink helper. Fixes a legacy bug where the
// blink-period argument passed to attach_flip() was silently ignored
// (the Ticker was always hardcoded to a 1-second period regardless of the
// caller's requested value) — here, the period is always honored.
class BlinkController {
public:
  void begin(uint16_t periodMs = 500) {
    periodMs_ = periodMs;
    lastToggleMs_ = 0;
    on_ = true;
    enabled_ = false;
  }

  void setEnabled(bool enabled) {
    if (enabled && !enabled_) lastToggleMs_ = 0;   // force an immediate first toggle
    enabled_ = enabled;
    if (!enabled) on_ = true;
  }

  // Returns true when the on/off phase changed this call (caller should
  // repaint LEDs).
  bool tick(uint32_t nowMs) {
    if (!enabled_) return false;
    if ((uint32_t)(nowMs - lastToggleMs_) < periodMs_) return false;
    lastToggleMs_ = nowMs;
    on_ = !on_;
    return true;
  }

  bool isOn() const { return on_; }
  bool isEnabled() const { return enabled_; }

private:
  uint16_t periodMs_ = 500;
  uint32_t lastToggleMs_ = 0;
  bool on_ = true;
  bool enabled_ = false;
};

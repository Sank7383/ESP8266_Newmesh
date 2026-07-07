#pragma once
#include <Arduino.h>

// Brief audible feedback on every button press. This was present in the
// legacy firmware (buz_pin/buz_pinnew, auto-off after ~300ms) but was
// dropped entirely in the rewrite — GPIO5 matches the legacy default pin
// and isn't used by any button/LED/RS485 variant in this firmware.
#define BUZZER_PIN 5

class BuzzerController {
public:
  void begin(uint16_t durationMs = 120) {
    durationMs_ = durationMs;
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
  }

  void trigger(uint32_t nowMs) {
    digitalWrite(BUZZER_PIN, HIGH);
    offAtMs_ = nowMs + durationMs_;
    active_ = true;
  }

  void tick(uint32_t nowMs) {
    if (active_ && (int32_t)(nowMs - offAtMs_) >= 0) {
      digitalWrite(BUZZER_PIN, LOW);
      active_ = false;
    }
  }

private:
  uint16_t durationMs_ = 120;
  uint32_t offAtMs_ = 0;
  bool active_ = false;
};

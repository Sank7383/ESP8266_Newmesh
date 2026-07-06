#pragma once
#include "IButtonInput.h"
#include "DebounceEngine.h"

// 74HC165 parallel-load shift register button reader. Serves BOTH
// ButtonVariant::SHIFTREG_5BTN and ButtonVariant::SHIFTREG_3BTN — the
// "3-button" hardware is the same physical read path with 4 of the 7
// logical slots mapped to ButtonAction::NONE in the configured
// ButtonActionMap, not a separate driver.
class ButtonInputShiftReg5 : public IButtonInput {
public:
  void begin(const DeviceConfig &cfg) override;
  bool poll(uint32_t nowMs, ButtonEvent &out) override;

private:
  static const uint8_t PALM_BIT = 6;

  uint8_t latchPin_ = 16, clockPin_ = 15, dataPin_ = 4;
  ButtonActionMap map_{};
  DebounceEngine debounce_;
  bool lastPalmAttached_ = false;

  uint8_t readRawByte() const;
  int8_t decodeSlot(uint8_t rawByte, uint8_t pcbRevision) const;
};

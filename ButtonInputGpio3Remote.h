#pragma once
#include "IButtonInput.h"
#include "DebounceEngine.h"

// Direct-GPIO "3-button remote" strategy. Despite the name (inherited from
// the legacy firmware/field terminology), the reference hardware only wires
// 2 physical GPIOs (cancel + call) on different pins than ButtonInputGpio2.
class ButtonInputGpio3Remote : public IButtonInput {
public:
  void begin(const DeviceConfig &cfg) override;
  bool poll(uint32_t nowMs, ButtonEvent &out) override;

private:
  uint8_t cancelPin_ = 16;
  uint8_t callPin_ = 2;
  ButtonActionMap map_{};
  DebounceEngine debounce_;
};

#pragma once
#include "IButtonInput.h"
#include "DebounceEngine.h"

// Direct-GPIO 2-button strategy (e.g. toilet pull-cord unit: call + cancel
// only). Physical slot 0 = cancel pin, slot 1 = call pin — reuses the same
// ButtonActionMap slot-index concept as the HC165 driver so the settings
// form's per-slot action dropdowns apply uniformly across variants.
class ButtonInputGpio2 : public IButtonInput {
public:
  void begin(const DeviceConfig &cfg) override;
  bool poll(uint32_t nowMs, ButtonEvent &out) override;

private:
  uint8_t cancelPin_ = 2;
  uint8_t callPin_ = 4;
  ButtonActionMap map_{};
  DebounceEngine debounce_;
};

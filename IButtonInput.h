#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"

struct ButtonEvent {
  ButtonAction action = ButtonAction::NONE;
  bool isLongPress = false;
  bool palmAttached = false;
};

// Abstract strategy for reading physical button hardware. Concrete
// implementations are statically allocated (see ButtonInputFactory) — never
// heap-constructed/destroyed at runtime.
class IButtonInput {
public:
  virtual void begin(const DeviceConfig &cfg) = 0;
  // Returns true exactly once per confirmed (debounced) button transition.
  virtual bool poll(uint32_t nowMs, ButtonEvent &out) = 0;
  virtual ~IButtonInput() = default;
};

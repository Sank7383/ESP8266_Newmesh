#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"

enum class ButtonEventType : uint8_t { PRESSED, RELEASED };

struct ButtonEvent {
  ButtonAction action = ButtonAction::NONE;
  // PRESSED fires the instant a press debounces stable (give feedback,
  // e.g. buzzer, here). RELEASED fires on release, with isLongPress known
  // by then — the call-state action should be applied on RELEASED, not
  // PRESSED, since short vs long press isn't decided until release.
  ButtonEventType type = ButtonEventType::RELEASED;
  bool isLongPress = false;
  bool palmAttached = false;
};

// Abstract strategy for reading physical button hardware. Concrete
// implementations are statically allocated (see ButtonInputFactory) — never
// heap-constructed/destroyed at runtime.
class IButtonInput {
public:
  virtual void begin(const DeviceConfig &cfg) = 0;
  // Returns true exactly once per PRESSED and once per RELEASED transition
  // (see ButtonEventType) — i.e. up to twice per physical button press.
  virtual bool poll(uint32_t nowMs, ButtonEvent &out) = 0;
  virtual ~IButtonInput() = default;
};

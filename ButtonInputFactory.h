#pragma once
#include "IButtonInput.h"

namespace ButtonInputFactory {
  // Returns a statically-allocated instance selected by cfg.buttonVariant,
  // or nullptr for ButtonVariant::NONE (e.g. a door-indicator role with no
  // physical buttons at all).
  IButtonInput* create(const DeviceConfig &cfg);
}

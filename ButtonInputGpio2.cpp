#include "ButtonInputGpio2.h"

void ButtonInputGpio2::begin(const DeviceConfig &cfg) {
  cancelPin_ = 2;
  callPin_ = 4;
  map_ = cfg.buttonMap;

  pinMode(cancelPin_, INPUT_PULLUP);
  pinMode(callPin_, INPUT_PULLUP);

  debounce_.begin(50, 800);
}

bool ButtonInputGpio2::poll(uint32_t nowMs, ButtonEvent &out) {
  bool cancelActive = (digitalRead(cancelPin_) == LOW);
  bool callActive = (digitalRead(callPin_) == LOW);

  // Cancel takes priority if both are pressed simultaneously, matching the
  // legacy firmware's evaluation order.
  uint8_t logicalKey = DEBOUNCE_NO_KEY;
  if (cancelActive) logicalKey = 0;
  else if (callActive) logicalKey = 1;

  uint8_t releasedSlot;
  bool isLongPress;
  if (!debounce_.update(logicalKey, nowMs, releasedSlot, isLongPress)) return false;
  if (releasedSlot > 1) return false;

  ButtonAction action = map_.slot[releasedSlot];
  if (action == ButtonAction::NONE) return false;

  out = ButtonEvent{};
  out.action = action;
  out.isLongPress = isLongPress;
  return true;
}

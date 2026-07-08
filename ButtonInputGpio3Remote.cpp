#include "ButtonInputGpio3Remote.h"

void ButtonInputGpio3Remote::begin(const DeviceConfig &cfg) {
  cancelPin_ = 16;
  callPin_ = 2;
  map_ = cfg.buttonMap;

  pinMode(cancelPin_, INPUT_PULLUP);
  pinMode(callPin_, INPUT_PULLUP);

  debounce_.begin(50, 800);
}

bool ButtonInputGpio3Remote::poll(uint32_t nowMs, ButtonEvent &out) {
  bool cancelActive = (digitalRead(cancelPin_) == LOW);
  bool callActive = (digitalRead(callPin_) == LOW);

  uint8_t logicalKey = DEBOUNCE_NO_KEY;
  if (cancelActive) logicalKey = 0;
  else if (callActive) logicalKey = 1;

  uint8_t slotIdx;
  bool isLongPress;
  DebounceResult result = debounce_.update(logicalKey, nowMs, slotIdx, isLongPress);
  if (result == DebounceResult::NONE) return false;
  if (slotIdx > 1) return false;

  ButtonAction action = map_.slot[slotIdx];
  if (action == ButtonAction::NONE) return false;

  out = ButtonEvent{};
  out.action = action;
  out.type = (result == DebounceResult::PRESSED) ? ButtonEventType::PRESSED : ButtonEventType::RELEASED;
  out.isLongPress = isLongPress;
  return true;
}

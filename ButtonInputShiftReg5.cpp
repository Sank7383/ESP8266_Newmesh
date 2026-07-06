#include "ButtonInputShiftReg5.h"

// Two known bit-pattern tables for the 74HC165 button PCB, in fixed slot
// order {cancel, call, toi, extra, blue, attend, ap} — ported verbatim from
// the legacy firmware's button_array[7].
static const uint8_t BUTTON_ARRAY_OLD_PCB[7]     = { 0xfe, 0xfd, 0xdf, 0xf7, 0xef, 0xfb, 0xe7 };
static const uint8_t BUTTON_ARRAY_NEW_STICKER[7] = { 0x7d, 0x7e, 0x5f, 0x77, 0x6f, 0x7b, 0x67 };

void ButtonInputShiftReg5::begin(const DeviceConfig &cfg) {
  latchPin_ = 16;
  clockPin_ = 15;
  dataPin_  = 4;
  map_ = cfg.buttonMap;

  pinMode(latchPin_, OUTPUT);
  pinMode(clockPin_, OUTPUT);
  pinMode(dataPin_, INPUT_PULLUP);
  digitalWrite(clockPin_, HIGH);
  digitalWrite(latchPin_, HIGH);

  debounce_.begin(50, 800);
}

uint8_t ButtonInputShiftReg5::readRawByte() const {
  digitalWrite(latchPin_, LOW);
  delayMicroseconds(5);
  digitalWrite(latchPin_, HIGH);

  uint8_t incoming = shiftIn(dataPin_, clockPin_, MSBFIRST);

  digitalWrite(clockPin_, LOW);
  digitalWrite(clockPin_, HIGH);

  return incoming;
}

int8_t ButtonInputShiftReg5::decodeSlot(uint8_t rawByte, uint8_t pcbRevision) const {
  // Normalize out the palm-accessory bit before matching against the table —
  // it is a separate accessory-presence signal, not a button.
  uint8_t normalized = rawByte;
  bitSet(normalized, PALM_BIT);

  const uint8_t *table = (pcbRevision == (uint8_t)ButtonPcbRevision::OLD_PCB)
                            ? BUTTON_ARRAY_OLD_PCB
                            : BUTTON_ARRAY_NEW_STICKER;

  for (uint8_t i = 0; i < 7; i++) {
    if (normalized == table[i]) return (int8_t)i;
  }
  return -1;
}

bool ButtonInputShiftReg5::poll(uint32_t nowMs, ButtonEvent &out) {
  uint8_t raw = readRawByte();

  bool palmAttached = !bitRead(raw, PALM_BIT);
  if (palmAttached != lastPalmAttached_) {
    lastPalmAttached_ = palmAttached;
    out = ButtonEvent{};
    out.action = ButtonAction::PALM_ATTACHED;
    out.palmAttached = palmAttached;
    return true;
  }

  // myConfig.buttonPcbRevision is read fresh each call via begin()'s captured
  // map_/pcb selection — cfg is captured once in begin(); revision is stable
  // for the device's lifetime between reboots (changed via settings + reboot).
  int8_t slot = decodeSlot(raw, (uint8_t)myConfig.buttonPcbRevision);
  uint8_t logicalKey = (slot < 0) ? DEBOUNCE_NO_KEY : (uint8_t)slot;

  uint8_t releasedSlot;
  bool isLongPress;
  if (!debounce_.update(logicalKey, nowMs, releasedSlot, isLongPress)) return false;
  if (releasedSlot >= 7) return false;

  ButtonAction action = map_.slot[releasedSlot];
  if (action == ButtonAction::NONE) return false;   // remapped-out slot (e.g. "3-button" variant)

  out = ButtonEvent{};
  out.action = action;
  out.isLongPress = isLongPress;
  out.palmAttached = lastPalmAttached_;
  return true;
}

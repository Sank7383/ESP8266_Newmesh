#include "LedStripController.h"
#include "MeshNowExports.h"

// RGB/GRB/BRG wiring-compensation rows x named color slots (LedColorSlot).
// Row 0 holds the canonical (true RGB) color for each slot; rows 1-2 are
// the same colors with channels permuted to compensate for GRB/BRG-wired
// LED chips while FastLED itself is always told "RGB" (see the pin-binding
// note in the header). These are a reasonable starting point, not a
// hardware-measured calibration — retune via this table if a given strip's
// colors look swapped on real hardware.
static const uint32_t LED_COLOUR[3][LED_COLOR_SLOT_COUNT] = {
  // CLEAR,    CALL_RED, HK_PINK,  EXTRA_ORANGE, CODE_BLUE, CARE_GREEN, DISCONNECT_PINK, DISCONNECT_WHITE, IDLE_ON
  {0x000000, 0xFF0000, 0xFF1493, 0xFF7F00,     0x0000FF,  0x00FF00,   0xFF1493,        0xFFFFFF,         0xFFFF00},   // RGB
  {0x000000, 0x00FF00, 0x14FF93, 0x7FFF00,     0x0000FF,  0xFF0000,   0x14FF93,        0xFFFFFF,         0xFFFF00},   // GRB
  {0x000000, 0x00FF00, 0x93FF14, 0x00FF7F,     0xFF0000,  0x0000FF,   0x93FF14,        0xFFFFFF,         0x00FFFF},   // BRG
};

void LedStripController::begin(const DeviceConfig &cfg) {
  FastLED.addLeds<WS2812, LED_DATA_PIN, RGB>(leds_, MAX_LEDS);
  blink_.begin(500);
  linkBlink_.begin(500);
  refreshConfig(cfg);
  dirty_ = true;
  repaint();
}

void LedStripController::refreshConfig(const DeviceConfig &cfg) {
  // Called every loop (see the .ino) so Form 3 saves take effect without a
  // reboot — MUST only mark dirty on an actual change, or repaint()/
  // FastLED.show()/logPixels() would fire every single loop iteration
  // forever, flooding the debug log and burning cycles on a no-op repaint.
  DeviceRole newRole = (DeviceRole)cfg.deviceRole;
  uint8_t newColorRow = cfg.color_row_indi <= 2 ? cfg.color_row_indi : 0;
  uint8_t newCount = cfg.ledCall.count;
  if (newCount == 0 || newCount > MAX_LEDS) newCount = 8;
  bool newDefaultLedOn = cfg.default_led;
  uint32_t newIdleIntervalMs = (uint32_t)((cfg.Indicator_timer >= 5) ? cfg.Indicator_timer : 10) * 1000UL;
  uint8_t newBrightness = cfg.ledBrightness ? cfg.ledBrightness : 80;

  bool changed = (newRole != role_) || (newColorRow != colorRow_) || (newCount != activeCount_) ||
                 (newDefaultLedOn != defaultLedOn_) || (newIdleIntervalMs != idleHeartbeatIntervalMs_) ||
                 (newBrightness != FastLED.getBrightness());
  if (!changed) return;

  role_ = newRole;
  colorRow_ = newColorRow;
  activeCount_ = newCount;
  defaultLedOn_ = newDefaultLedOn;
  idleHeartbeatIntervalMs_ = newIdleIntervalMs;
  FastLED.setBrightness(newBrightness);
  dirty_ = true;
}

uint32_t LedStripController::colorFor(LedColorSlot slot) const {
  return LED_COLOUR[colorRow_][(uint8_t)slot];
}

uint32_t LedStripController::idleColorRaw() const {
  // "Default LED On" (Form 3): ON = steady idle color when nothing's
  // active; OFF = normally dark, with a brief IDLE_ON pulse every
  // idleHeartbeatIntervalMs_ (see tick()) as a heartbeat.
  if (defaultLedOn_) return colorFor(LedColorSlot::IDLE_ON);
  return colorFor(idlePulseOn_ ? LedColorSlot::IDLE_ON : LedColorSlot::CLEAR);
}

LedColorSlot LedStripController::colorSlotForCallState(CallState state) {
  switch (state) {
    case CallState::CALL:        return LedColorSlot::CALL_RED;
    case CallState::CARE:        return LedColorSlot::CARE_GREEN;
    case CallState::EXTRA_HELP:  return LedColorSlot::EXTRA_ORANGE;
    case CallState::CODE_BLUE:   return LedColorSlot::CODE_BLUE;
    case CallState::TOILET_CALL: return LedColorSlot::CALL_RED;   // same red as a bed call, per spec
    default:                     return LedColorSlot::CLEAR;
  }
}

void LedStripController::setCallZone(const CallStateMachine::CallStatus &status, const CallRulesetConfig &ruleset) {
  bool changed = (status.mainState != lastStatus_.mainState) ||
                 (status.toiletCallActive != lastStatus_.toiletCallActive) ||
                 (status.housekeeping != lastStatus_.housekeeping);
  if (!changed) return;
  lastStatus_ = status;
  if (status.mainState == CallState::CUSTOM) {
    customColorIdx_ = ruleset.customState.ledColorIndex < LED_COLOR_SLOT_COUNT
                         ? ruleset.customState.ledColorIndex
                         : (uint8_t)LedColorSlot::CLEAR;
  }

  bool anyActiveCall = (status.mainState != CallState::IDLE) || status.toiletCallActive;

  // Housekeeping + an active call blinks between the housekeeping color and
  // the specific active-call color — richer local feedback than the single
  // collapsed "status 8" the server sees (CallStateMachine::reportedStatusCode).
  blink_.setEnabled(status.housekeeping && anyActiveCall);

  dirty_ = true;
}

void LedStripController::setAggregateZone(const uint8_t *roomStates, uint8_t count) {
  uint8_t maxStatus = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (roomStates[i] > maxStatus) maxStatus = roomStates[i];
  }
  uint8_t newIdx = (maxStatus == 0) ? (uint8_t)LedColorSlot::CLEAR
                                     : (uint8_t)min((int)maxStatus, LED_COLOR_SLOT_COUNT - 1);
  if (newIdx == aggregatePriorityColorIdx_) return;
  aggregatePriorityColorIdx_ = newIdx;
  dirty_ = true;
}

void LedStripController::setLinkStatus(bool networkUp, bool serverUp) {
  if (networkUp == networkUp_ && serverUp == serverUp_) return;
  networkUp_ = networkUp;
  serverUp_ = serverUp;
  linkBlink_.setEnabled(!networkUp_ || !serverUp_);
  dirty_ = true;
}

void LedStripController::repaint() {
  // Disconnected states take total priority over call-state display —
  // matches the reference firmware's setLedStatus(), which returns
  // immediately once it decides the device has no network/server, before
  // ever looking at call state. !networkUp_ (no WiFi/Ethernet at all) is
  // more severe than !serverUp_ (WiFi's fine, but the uplink server isn't
  // answering) so it wins when both are true.
  if (!networkUp_ || !serverUp_) {
    LedColorSlot onSlot = !networkUp_ ? LedColorSlot::DISCONNECT_WHITE : LedColorSlot::DISCONNECT_PINK;
    CRGB c = (CRGB)(linkBlink_.isOn() ? colorFor(onSlot) : colorFor(LedColorSlot::CLEAR));
    for (uint8_t i = 0; i < activeCount_; i++) leds_[i] = c;
    FastLED.show();
    FastLED.show();
    logPixels(!networkUp_ ? "no-network" : "no-server");
    return;
  }

  // The color for whatever's currently active, as a raw table lookup —
  // CallState::CUSTOM is the one case that doesn't map to a fixed
  // LedColorSlot, it uses the site-configured customColorIdx_ instead.
  auto activeCallColor = [&]() -> uint32_t {
    if (lastStatus_.mainState == CallState::CUSTOM) return LED_COLOUR[colorRow_][customColorIdx_];
    if (lastStatus_.mainState != CallState::IDLE) return colorFor(colorSlotForCallState(lastStatus_.mainState));
    return colorFor(LedColorSlot::CALL_RED);   // toilet call
  };

  uint32_t callColorRaw;
  if (lastStatus_.housekeeping) {
    bool anyActiveCall = (lastStatus_.mainState != CallState::IDLE) || lastStatus_.toiletCallActive;
    if (anyActiveCall) {
      callColorRaw = blink_.isOn() ? activeCallColor() : colorFor(LedColorSlot::HK_PINK);
    } else {
      callColorRaw = colorFor(LedColorSlot::HK_PINK);   // steady housekeeping-only
    }
  } else if (lastStatus_.mainState != CallState::IDLE || lastStatus_.toiletCallActive) {
    callColorRaw = activeCallColor();
  } else {
    callColorRaw = idleColorRaw();
  }

  CRGB callColor = (CRGB)callColorRaw;

  if (role_ == DeviceRole::DOOR_INDICATOR) {
    bool aggregateIdle = (aggregatePriorityColorIdx_ == (uint8_t)LedColorSlot::CLEAR);
    CRGB c = aggregateIdle ? (CRGB)idleColorRaw() : (CRGB)LED_COLOUR[colorRow_][aggregatePriorityColorIdx_];
    for (uint8_t i = 0; i < activeCount_; i++) leds_[i] = c;
  } else {
    if (activeCount_ > 0) leds_[0] = callColor;
    CRGB aggColor = (CRGB)LED_COLOUR[colorRow_][aggregatePriorityColorIdx_];
    for (uint8_t i = 1; i < activeCount_; i++) leds_[i] = aggColor;
  }

  FastLED.show();
  FastLED.show();
  logPixels("call");
}

void LedStripController::logPixels(const char *reason) const {
  String msg = "LED PIXELS [" + String(reason) + "] row=" + String(colorRow_) +
               " count=" + String(activeCount_) + " bright=" + String(FastLED.getBrightness()) + " :";
  for (uint8_t i = 0; i < activeCount_; i++) {
    char hex[16];
    sprintf(hex, " [%u]#%02X%02X%02X", i, leds_[i].r, leds_[i].g, leds_[i].b);
    msg += hex;
  }
  debugdata(msg.c_str());
}

void LedStripController::tick(uint32_t nowMs) {
  if (blink_.tick(nowMs)) dirty_ = true;
  if (linkBlink_.tick(nowMs)) dirty_ = true;

  // Idle heartbeat pulse: only matters visually while idleColorRaw() is
  // actually in use (nothing active) — runs unconditionally here so it's
  // always in the right phase whenever that becomes true, rather than
  // needing to know "is the device idle right now" itself.
  if (!defaultLedOn_) {
    uint32_t phaseMs = idlePulseOn_ ? IDLE_PULSE_ON_MS : idleHeartbeatIntervalMs_;
    if ((nowMs - idleTimerMs_) >= phaseMs) {
      idleTimerMs_ = nowMs;
      idlePulseOn_ = !idlePulseOn_;
      dirty_ = true;
    }
  }

  if (!dirty_) return;
  dirty_ = false;
  repaint();
}

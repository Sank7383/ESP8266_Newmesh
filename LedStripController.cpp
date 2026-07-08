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
  bool newShareUnit = bedToiletShareUnit(cfg);
  bool newToiletIndicationOnIdle = cfg.toiletIndicationOnIdle;

  bool changed = (newRole != role_) || (newColorRow != colorRow_) || (newCount != activeCount_) ||
                 (newDefaultLedOn != defaultLedOn_) || (newIdleIntervalMs != idleHeartbeatIntervalMs_) ||
                 (newBrightness != FastLED.getBrightness()) || (newShareUnit != bedToiletShareUnit_) ||
                 (newToiletIndicationOnIdle != toiletIndicationOnIdle_);
  if (!changed) return;

  role_ = newRole;
  colorRow_ = newColorRow;
  activeCount_ = newCount;
  defaultLedOn_ = newDefaultLedOn;
  idleHeartbeatIntervalMs_ = newIdleIntervalMs;
  bedToiletShareUnit_ = newShareUnit;
  toiletIndicationOnIdle_ = newToiletIndicationOnIdle;
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
    // HOUSEKEEPING/HOUSEKEEPING_CALL only ever reach this function via
    // colorSlotForStatusCode() below — this device's OWN housekeeping flag
    // is tracked separately (CallStatus::housekeeping) and never folded
    // into mainState, so a local call never hits this case.
    case CallState::HOUSEKEEPING:      return LedColorSlot::HK_PINK;
    case CallState::HOUSEKEEPING_CALL: return LedColorSlot::HK_PINK;
    default:                     return LedColorSlot::CLEAR;
  }
}

LedColorSlot LedStripController::colorSlotForStatusCode(uint8_t code) {
  // Peer/remote statuses only ever arrive as the plain reported number
  // (CallStateMachine::reportedStatusCode()'s output), never a CallState —
  // route through the same names colorSlotForCallState uses so a "5" from
  // the network looks the same as a locally-driven TOILET_CALL.
  return colorSlotForCallState((CallState)code);
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

  // Housekeeping + an active BED call blinks leds_[0] between the
  // housekeeping color and the specific active-call color — richer local
  // feedback than the single collapsed "status 8" the server sees
  // (CallStateMachine::reportedStatusCode). Toilet only counts here when
  // bedToiletShareUnit_ — same physical unit, same machineid == toiletid
  // (or toiletid==0), so there's no separate device to show it on and it
  // mirrors onto leds_[0] too (still reported as TOILET_CALL(5) to the
  // server, never folded into the bed's own reported code — this only
  // affects the LOCAL LED, see repaint()). A distinct toiletid never
  // reaches this — that case is leds_[1]-only, fed by setToiletRemoteStatus().
  bool bedZoneActive = (status.mainState != CallState::IDLE) || (bedToiletShareUnit_ && status.toiletCallActive);
  // CODE_BLUE is the one state that does NOT dual-blink under housekeeping
  // — per spec it shows steady blue only, even though the housekeeping
  // flag (status.housekeeping) stays true internally the whole time (see
  // CallStateMachine::reportedStatusCode() for the matching reported-code
  // exception). Disabling blink_ here makes repaint() fall through to
  // ownActiveColor() unconditionally for this state — BlinkController
  // forces isOn()==true whenever it's disabled, so no separate "steady
  // blue" branch is needed in repaint() itself. Every OTHER active state
  // (CALL/CARE/EXTRA_HELP/TOILET_CALL-mirror) still dual-blinks as before.
  bool blinkEligible = bedZoneActive && (status.mainState != CallState::CODE_BLUE);
  blink_.setEnabled(status.housekeeping && blinkEligible);

  dirty_ = true;
}

void LedStripController::setAggregateZone(const uint8_t *roomStates, uint8_t count) {
  uint8_t maxStatus = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (roomStates[i] > maxStatus) maxStatus = roomStates[i];
  }
  if (maxStatus == aggregateMaxStatus_) return;
  aggregateMaxStatus_ = maxStatus;
  dirty_ = true;
}

void LedStripController::setToiletRemoteStatus(uint8_t statusCode) {
  if (statusCode == toiletRemoteStatus_) return;
  toiletRemoteStatus_ = statusCode;
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

  // leds_[0] — this device's OWN call status, PLUS a mirrored toilet-call
  // color when bedToiletShareUnit_ (same physical unit, no separate toilet
  // device to show it on instead — see setCallZone()). A distinct toiletid
  // never affects leds_[0]; that case is leds_[1]-only.
  bool bedOwnActive = (lastStatus_.mainState != CallState::IDLE);
  bool mirrorToiletOnBed = bedToiletShareUnit_ && lastStatus_.toiletCallActive;
  auto ownActiveColor = [&]() -> uint32_t {
    if (bedOwnActive) {
      if (lastStatus_.mainState == CallState::CUSTOM) return LED_COLOUR[colorRow_][customColorIdx_];
      return colorFor(colorSlotForCallState(lastStatus_.mainState));
    }
    // Only reached when mirrorToiletOnBed is what's making the zone active —
    // same red a bed Call would show, but the server still gets TOILET_CALL(5)
    // via reportedStatusCode(), not CALL(1) — this only changes the LED.
    return colorFor(colorSlotForCallState(CallState::TOILET_CALL));
  };

  bool bedZoneActive = bedOwnActive || mirrorToiletOnBed;
  uint32_t callColorRaw;
  if (lastStatus_.housekeeping) {
    callColorRaw = bedZoneActive ? (blink_.isOn() ? ownActiveColor() : colorFor(LedColorSlot::HK_PINK))
                                  : colorFor(LedColorSlot::HK_PINK);   // steady housekeeping-only
  } else if (bedZoneActive) {
    callColorRaw = ownActiveColor();
  } else {
    callColorRaw = idleColorRaw();
  }
  CRGB callColor = (CRGB)callColorRaw;

  // leds_[1] — the TOILET's status. Shared unit (toiletid==0 or ==this
  // device's own machineid): derived from this device's own toiletCallActive
  // flag. Distinct toiletid: derived from the last "j<toiletid>,<status>"
  // report seen for that id (setToiletRemoteStatus(), fed by the .ino ONLY
  // from this bed unit's own button-routed sends and genuine incoming
  // "j<toiletid>,X" reports from the toilet's own device — never from the
  // generic door/aggregate "highest across all rooms" computation, so this
  // zone can't be moved by unrelated server/room activity).
  bool toiletHasCall = bedToiletShareUnit_ ? lastStatus_.toiletCallActive : (toiletRemoteStatus_ != 0);
  uint32_t toiletColorRaw;
  if (toiletHasCall) {
    toiletColorRaw = bedToiletShareUnit_ ? colorFor(colorSlotForCallState(CallState::TOILET_CALL))
                                          : colorFor(colorSlotForStatusCode(toiletRemoteStatus_));
  } else {
    // "Toilet Idle Indication" Off: stay fully dark when there's no call,
    // instead of idleColorRaw()'s steady/heartbeat idle display.
    toiletColorRaw = toiletIndicationOnIdle_ ? idleColorRaw() : colorFor(LedColorSlot::CLEAR);
  }
  CRGB toiletColor = (CRGB)toiletColorRaw;

  // leds_[2..] (or the whole strip for a door indicator) — the multi-room
  // aggregate/door-priority zone, fed by LedAggregator via setAggregateZone().
  uint32_t aggRaw = (aggregateMaxStatus_ == 0) ? idleColorRaw() : colorFor(colorSlotForStatusCode(aggregateMaxStatus_));
  CRGB aggColor = (CRGB)aggRaw;

  if (role_ == DeviceRole::DOOR_INDICATOR) {
    for (uint8_t i = 0; i < activeCount_; i++) leds_[i] = aggColor;
  } else {
    if (activeCount_ > 0) leds_[0] = callColor;
    if (activeCount_ > 1) leds_[1] = toiletColor;
    for (uint8_t i = 2; i < activeCount_; i++) leds_[i] = aggColor;
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

#pragma once
#include "ILedController.h"
#include "BlinkController.h"
#include <FastLED.h>

// NOTE on LED data pin: FastLED's addLeds<CHIPSET, PIN, ORDER>() binds the
// data pin at COMPILE time (a template parameter), which is fundamentally
// in tension with "everything runtime configurable". Since a single flashed
// firmware image is meant to run unmodified across bed/toilet/door-indicator
// PCBs, this firmware standardizes on GPIO0 for the LED data line across all
// roles (matching the legacy firmware's default for every PCB revision
// except one deprecated door-indicator board that used GPIO13 — that one
// specific old board would need a hardware pin bridge or a separate build
// to run this unified image; flagged as a known limitation, not silently
// dropped).
#define LED_DATA_PIN 0

// Named color slots, matching the reference button.h's LedStates exactly —
// this is the ONE place a status maps to a color, by name, not by bit math.
enum class LedColorSlot : uint8_t {
  CLEAR = 0,           // fully off
  CALL_RED = 1,        // call, or toilet-call (same color, different zone)
  HK_PINK = 2,          // housekeeping only
  EXTRA_ORANGE = 3,     // extra-help
  CODE_BLUE = 4,        // code-blue
  CARE_GREEN = 5,       // care/attend acknowledged
  DISCONNECT_PINK = 6,  // reserved: local-link-down blink color
  DISCONNECT_WHITE = 7, // reserved: upstream-link-down blink color
  IDLE_ON = 8,          // steady/heartbeat "device is alive, nothing active" color — see Default LED On
};
#define LED_COLOR_SLOT_COUNT 9

class LedStripController : public ILedController {
public:
  void begin(const DeviceConfig &cfg) override;
  void refreshConfig(const DeviceConfig &cfg) override;
  void setCallZone(const CallStateMachine::CallStatus &status, const CallRulesetConfig &ruleset) override;
  void setAggregateZone(const uint8_t *roomStates, uint8_t count) override;
  void setToiletRemoteStatus(uint8_t statusCode) override;
  void setLinkStatus(bool networkUp, bool serverUp) override;
  void tick(uint32_t nowMs) override;

private:
  CRGB leds_[MAX_LEDS];
  DeviceRole role_ = DeviceRole::BED_UNIT;
  uint8_t colorRow_ = 0;
  uint8_t activeCount_ = 8;

  CallStateMachine::CallStatus lastStatus_;
  uint8_t customColorIdx_ = (uint8_t)LedColorSlot::CLEAR;   // raw color-table index for CallState::CUSTOM
  // Both hold a raw server status code (CallStateMachine::reportedStatusCode()
  // range, 0-8 + custom), NOT a LedColorSlot index — colorSlotForStatusCode()
  // maps one to the other at paint time.
  uint8_t aggregateMaxStatus_ = 0;
  uint8_t toiletRemoteStatus_ = 0;

  // leds_[1] zone: derived locally from lastStatus_.toiletCallActive when
  // this bed unit IS the toilet's own device (myConfig.toiletid==0 or
  // ==machineid); derived from toiletRemoteStatus_ instead when toiletid
  // names a distinct device — see setToiletRemoteStatus().
  bool bedToiletShareUnit_ = true;
  // "Toilet Idle Indication" (myConfig.toiletIndicationOnIdle, Form 3): only
  // affects leds_[1] when the toilet has NO active call — On (default)
  // shows idleColorRaw() there like every other zone; Off forces it fully
  // dark instead, regardless of Default LED On. An active toilet call
  // always shows either way.
  bool toiletIndicationOnIdle_ = true;

  bool networkUp_ = true;
  bool serverUp_ = true;

  // "Default LED On" (myConfig.default_led): when true, idle (nothing
  // active) shows a steady IDLE_ON color. When false, idle is normally off
  // and IDLE_ON only flashes briefly every idleHeartbeatIntervalMs_ as a
  // heartbeat — matches the behavior described for the Form 3 field.
  bool defaultLedOn_ = false;
  uint32_t idleHeartbeatIntervalMs_ = 10000;
  uint32_t idleTimerMs_ = 0;
  bool idlePulseOn_ = false;
  static const uint32_t IDLE_PULSE_ON_MS = 300;

  BlinkController blink_;       // housekeeping+active-call color blink
  BlinkController linkBlink_;   // disconnect (white/pink) blink — independent of the above
  bool dirty_ = true;

  uint32_t colorFor(LedColorSlot slot) const;
  uint32_t idleColorRaw() const;
  static LedColorSlot colorSlotForCallState(CallState state);
  // Maps a raw reported status CODE (as opposed to colorSlotForCallState's
  // CallState enum) to a display color — used for the aggregate/door zone
  // and the remote-toilet zone, both of which only ever see the plain
  // number a peer device reported, not a CallState.
  static LedColorSlot colorSlotForStatusCode(uint8_t code);
  void repaint();
  // Dumps every active pixel's actual RGB hex value over debugdata() (visible
  // on the /forms page's live debug log) — for bench-testing when the strip
  // doesn't look right, to see exactly what the firmware thinks it's sending
  // versus what physically lights up.
  void logPixels(const char *reason) const;
};

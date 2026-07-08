#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"
#include "CallStateMachine.h"

class ILedController {
public:
  virtual void begin(const DeviceConfig &cfg) = 0;
  // Re-reads the small set of LED-relevant fields (color row, brightness,
  // active LED count, device role, Default LED On, idle heartbeat interval)
  // from myConfig. Settings-form saves only write to myConfig/EEPROM — they
  // don't otherwise reach a controller whose fields were captured once in
  // begin() — so this must be called every loop for a Form 3 change (Color
  // Row, Brightness, Active LED Count, Default LED On) to take effect
  // without requiring a reboot.
  virtual void refreshConfig(const DeviceConfig &cfg) = 0;
  // Takes the full CallStatus (not just the collapsed reported number) so
  // the display can show richer local feedback than the server sees — e.g.
  // blinking between the housekeeping color and the specific active-call
  // color for status 8, instead of one flat combo color.
  virtual void setCallZone(const CallStateMachine::CallStatus &status, const CallRulesetConfig &ruleset) = 0;
  // roomStates/count: the current multi-room aggregate snapshot from
  // LedAggregator, painted into the aggregate zone (whole strip for a
  // DOOR_INDICATOR role, or the trailing LEDs for a bed/toilet unit that
  // also mirrors aggregate site status).
  virtual void setAggregateZone(const uint8_t *roomStates, uint8_t count) = 0;
  // networkUp: WiFi STA associated / Ethernet linked (ITransport::isNetworkUp()).
  // serverUp: the configured uplink protocol is actually connected (ITransport::isLinkUp()).
  // When networkUp is false, the call zone blinks white/black, overriding
  // everything else. Else when serverUp is false, it blinks pink/black.
  // Both take priority over the normal call-state color — matches the
  // reference firmware's setLedStatus(), which returns immediately once it
  // decides the device is disconnected, before ever looking at call state.
  virtual void setLinkStatus(bool networkUp, bool serverUp) = 0;
  virtual void tick(uint32_t nowMs) = 0;   // services blink + FastLED.show() when dirty
  virtual ~ILedController() = default;
};

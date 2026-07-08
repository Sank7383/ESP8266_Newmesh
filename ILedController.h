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
  // LedAggregator, painted into the aggregate/door-indicator zone (the
  // whole strip for a DOOR_INDICATOR role, or leds_[2..] on a bed/toilet/
  // combo unit — see the zone layout note on setToiletRemoteStatus below).
  virtual void setAggregateZone(const uint8_t *roomStates, uint8_t count) = 0;
  // The strip on a bed unit is laid out as: leds_[0] = this device's own
  // call status, leds_[1] = the TOILET's status, leds_[2..] = the door-
  // indicator/aggregate zone (setAggregateZone above). When myConfig's
  // toiletid names a distinct device (not shared with this bed unit),
  // leds_[1] can't be derived from this device's own CallStatus — it has
  // to come from the last "j<toiletid>,<status>" mesh report seen for that
  // id (LedAggregator::statusFor(toiletid), looked up by the caller each
  // loop). Ignored when the toilet is this same unit (bedToiletShareUnit) —
  // in that case leds_[1] is derived from setCallZone()'s toiletCallActive
  // flag instead.
  virtual void setToiletRemoteStatus(uint8_t statusCode) = 0;
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

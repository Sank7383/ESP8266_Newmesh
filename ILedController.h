#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"
#include "CallStateMachine.h"

class ILedController {
public:
  virtual void begin(const DeviceConfig &cfg) = 0;
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
  virtual void tick(uint32_t nowMs) = 0;   // services blink + FastLED.show() when dirty
  virtual ~ILedController() = default;
};

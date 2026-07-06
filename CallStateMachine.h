#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"

// Pure, hardware-free call-state transition logic. No dependency on
// Arduino I/O beyond basic types, so it can be exercised standalone (e.g.
// from a serial test harness) independent of any button/LED/transport code.
//
// Deliberately NOT bit-packed: a device is always in exactly one named
// CallState (IDLE/CALL/CARE/EXTRA_HELP/CODE_BLUE/CUSTOM), plus two
// independent flags (toiletCallActive, housekeeping) that are never OR'd
// into that state number. reportedStatusCode() is the one place the two
// combine into the single number your server understands.
namespace CallStateMachine {

  struct CallStatus {
    CallState mainState = CallState::IDLE;
    bool toiletCallActive = false;
    bool housekeeping = false;
  };

  // Applies one button action to status IN PLACE. Returns true if the
  // action was legal and status changed; false if illegal in the current
  // state (status is left untouched). housekeepingBypassesCare should be
  // myConfig.houseKeepings — when true, EXTRA_HELP/CODE_BLUE are reachable
  // directly from an active CALL/TOILET_CALL; when false, CARE must be
  // pressed first (the legacy "5-button care and clear rule").
  bool apply(CallStatus &status, ButtonAction action, bool isLongPress,
             bool bedToiletShareUnit, const CallRulesetConfig &ruleset,
             bool housekeepingBypassesCare);

  // The single number reported to the server/mesh peers, matching your
  // server's existing status codes exactly: 0 idle, 1 call, 2 care,
  // 3 extra-help, 4 code-blue, 5 toilet-call, 7 housekeeping (idle),
  // 8 housekeeping + any active call/care/help/blue/toilet-call.
  uint8_t reportedStatusCode(const CallStatus &status, const CallRulesetConfig &ruleset);

}

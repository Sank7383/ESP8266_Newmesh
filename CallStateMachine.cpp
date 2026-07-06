#include "CallStateMachine.h"

namespace CallStateMachine {

bool apply(CallStatus &status, ButtonAction action, bool isLongPress,
           bool bedToiletShareUnit, const CallRulesetConfig &ruleset,
           bool housekeepingBypassesCare) {

  // Housekeeping is tracked independently of everything else below: a
  // short press sets it, a long press clears it. No gating on toilet/call
  // state — always legal.
  if (action == ButtonAction::HOUSEKEEPING) {
    status.housekeeping = !isLongPress;
    return true;
  }

  bool activeCallInProgress = (status.mainState != CallState::IDLE);

  // A pending toilet call (bed's mainState still IDLE) is its own small
  // ladder: it can be cancelled directly (unlike an active CALL) and
  // escalates through CARE/EXTRA_HELP/CODE_BLUE exactly like an active
  // CALL would.
  if (status.mainState == CallState::IDLE && status.toiletCallActive) {
    if (action == ButtonAction::CANCEL) {
      status.toiletCallActive = false;
      return true;
    }
    if (action == ButtonAction::CARE) {
      status.mainState = CallState::CARE;
      status.toiletCallActive = false;
      return true;
    }
    if (housekeepingBypassesCare && action == ButtonAction::EXTRA_HELP) {
      status.mainState = CallState::EXTRA_HELP;
      status.toiletCallActive = false;
      return true;
    }
    if (housekeepingBypassesCare && action == ButtonAction::CODE_BLUE) {
      status.mainState = CallState::CODE_BLUE;
      status.toiletCallActive = false;
      return true;
    }
    return false;
  }

  switch (status.mainState) {

    case CallState::IDLE:
      if (action == ButtonAction::CALL) {
        status.mainState = CallState::CALL;
        return true;
      }
      if (action == ButtonAction::TOILET_CALL) {
        // On a combined bed+toilet unit, a toilet call is blocked while
        // the bed already has its own active call in progress.
        if (bedToiletShareUnit && activeCallInProgress) return false;
        status.toiletCallActive = true;
        return true;
      }
      if (ruleset.directCodeBlueFromIdle && action == ButtonAction::CODE_BLUE) {
        status.mainState = CallState::CODE_BLUE;
        return true;
      }
      return false;

    case CallState::CALL:
      if (action == ButtonAction::CARE) {
        status.mainState = CallState::CARE;
        return true;
      }
      // A bare CALL can only be cancelled directly if this ruleset doesn't
      // require going through CARE first.
      if (!ruleset.careRequiredBeforeCancel && action == ButtonAction::CANCEL) {
        status.mainState = CallState::IDLE;
        return true;
      }
      if (housekeepingBypassesCare && action == ButtonAction::EXTRA_HELP) {
        status.mainState = CallState::EXTRA_HELP;
        return true;
      }
      if (housekeepingBypassesCare && action == ButtonAction::CODE_BLUE) {
        status.mainState = CallState::CODE_BLUE;
        return true;
      }
      return false;

    case CallState::CARE:
      if (action == ButtonAction::EXTRA_HELP) {
        status.mainState = CallState::EXTRA_HELP;
        return true;
      }
      if (action == ButtonAction::CODE_BLUE) {
        status.mainState = CallState::CODE_BLUE;
        return true;
      }
      if (action == ButtonAction::CANCEL) {
        status.mainState = CallState::IDLE;
        return true;
      }
      return false;

    case CallState::EXTRA_HELP:
      if (action == ButtonAction::CODE_BLUE) {
        status.mainState = CallState::CODE_BLUE;
        return true;
      }
      if (action == ButtonAction::CANCEL) {
        status.mainState = CallState::IDLE;
        return true;
      }
      return false;

    case CallState::CODE_BLUE:
      if (action == ButtonAction::CANCEL) {
        status.mainState = CallState::IDLE;
        return true;
      }
      return false;

    case CallState::CUSTOM:
      // Worked example: a site-specific custom state only ever clears via
      // CANCEL here. To trigger CUSTOM in the first place, wire whatever
      // this site's actual trigger is (a button, a long-press, or a
      // server-pushed command) to set status.mainState = CallState::CUSTOM
      // directly and skip apply() for that one transition — the rest of
      // this function (and reportedStatusCode() below) already know how
      // to display and report it once it's set.
      if (action == ButtonAction::CANCEL) {
        status.mainState = CallState::IDLE;
        return true;
      }
      return false;

    default:
      status.mainState = CallState::IDLE;
      return true;
  }
}

uint8_t reportedStatusCode(const CallStatus &status, const CallRulesetConfig &ruleset) {
  bool anyActiveCall = (status.mainState != CallState::IDLE) || status.toiletCallActive;

  if (status.housekeeping) {
    return anyActiveCall ? (uint8_t)CallState::HOUSEKEEPING_CALL : (uint8_t)CallState::HOUSEKEEPING;
  }

  if (status.mainState == CallState::CUSTOM && ruleset.customState.enabled) {
    return ruleset.customState.reportedCode;
  }

  if (status.mainState != CallState::IDLE) {
    return (uint8_t)status.mainState;
  }

  if (status.toiletCallActive) {
    return (uint8_t)CallState::TOILET_CALL;
  }

  return (uint8_t)CallState::IDLE;
}

}

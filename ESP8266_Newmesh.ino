/**
 * ESP8266 Modular Nurse-Call Firmware — Runtime-Variant Rewrite
 *
 * One firmware image where route (WiFi-mesh / RS485 / Ethernet), button
 * hardware (2-button GPIO / 5-button HC165 / 3-button remote), server
 * protocol (WebSocket / Socket.IO), device role (bed / toilet / door-
 * indicator), per-button action mapping, and call-state behavior rules are
 * all runtime-selectable and persisted to EEPROM via the settings web form
 * — see the plan at C:\Users\netsol\.claude\plans\write-a-code-for-wondrous-dawn.md
 * for the full architecture writeup.
 *
 * This file is the composition root ONLY: it loads config, constructs the
 * right concrete strategy for each runtime-selected variant, and runs the
 * main loop. All actual logic lives in the modules included below.
 */

// These must be defined before NewMeshNOW.h is included — they gate which
// legacy code paths inside that vendored header compile in.
// #define MQTTNOTREQUIRED
#define NURSECALLING
#define NURSECALLNEW
#define ESPNOWACTIVE

#include "NurseCallConfig.h"
#include "DeviceProtocol.h"
#include "NewMeshNOW.h"   // vendored mesh/webserver/websocket foundation — included exactly once, here only

#include "IButtonInput.h"
#include "ButtonInputFactory.h"
#include "CallStateMachine.h"
#include "ILedController.h"
#include "LedStripController.h"
#include "LedAggregator.h"
#include "ITransport.h"
#include "TransportFactory.h"
#include "LocalAccessStack.h"
#include "DeviceState.h"
#include "BuzzerController.h"

CallStateMachine::CallStatus g_callStatus;
LedAggregator g_roomAggregator;
BuzzerController g_buzzer;

static LedStripController s_ledStripController;

IButtonInput    *g_buttonInput = nullptr;
ILedController  *g_ledController = nullptr;
ITransport      *g_statusUplink = nullptr;
LocalAccessStack g_localAccess;

// The last status code sent under the SEPARATE toilet device's own identity
// (myConfig.toiletid), when !bedToiletShareUnit(myConfig) — there's no
// CallStatus for that case (this device's own g_callStatus never reflects
// it, see sendToiletStatus() below), so this is the only record of "what
// did we last tell the server the toilet's status is", used both for the
// periodic resend (loop()) and the reconnect resend (resendCurrentStatus()).
uint8_t g_lastToiletStatus = 0;

// Last time (millis()) EITHER sendBedStatus() or sendToiletStatus() actually
// sent something — drives the periodic resend in loop(): a real button-
// triggered send re-arms the interval so the periodic timer only ever fires
// when nothing has been reported for a full myConfig.statusReportIntervalSec,
// rather than redundantly repeating right after a real event.
uint32_t g_lastStatusReportMs = 0;

// Reports THIS device's own current combined status (CallStateMachine::
// reportedStatusCode()) under its own machineid. Shared by the button
// dispatch below, the periodic resend in loop(), and resendCurrentStatus()
// (called from NewMeshNOW.h once the uplink reconnects after being down).
void sendBedStatus() {
  g_lastStatusReportMs = millis();
  StatusPayload payload;
  payload.deviceId = (uint16_t)myConfig.machineid;
  payload.statusCode = CallStateMachine::reportedStatusCode(g_callStatus, myConfig.ruleset);
  debugdata(String("SEND: statusCode=" + String(payload.statusCode) +
                    " via route=" + String(myConfig.routeType) +
                    " linkUp=" + String(g_statusUplink->isLinkUp() ? 1 : 0)).c_str());
  g_statusUplink->sendStatus(payload);
}

// Reports a status for the SEPARATE toilet device's own identity — used
// when the toilet pull-cord is physically wired into THIS bed unit's shift
// register but toiletid names a distinct logical device on the mesh/
// server, not this one. Updates g_roomAggregator directly (not just
// sendStatus's own "j" broadcast) so leds_[1] reflects it immediately
// without waiting on a mesh round-trip back to this same device.
void sendToiletStatus(uint8_t status) {
  g_lastStatusReportMs = millis();
  g_lastToiletStatus = status;
  g_roomAggregator.updateRoom((uint16_t)myConfig.toiletid, status);
  StatusPayload toiletPayload;
  toiletPayload.deviceId = (uint16_t)myConfig.toiletid;
  toiletPayload.statusCode = status;
  debugdata(String("TOILET(separate id=" + String(myConfig.toiletid) + "): SEND status=" +
                    String(status)).c_str());
  g_statusUplink->sendStatus(toiletPayload);
}

void setup() {
  Serial.begin(115200);

  configLoad();
  g_roomAggregator.begin();
  g_buzzer.begin();

  g_buttonInput   = ButtonInputFactory::create(myConfig);   // nullptr for DeviceRole::DOOR_INDICATOR
  g_ledController = &s_ledStripController;
  g_statusUplink  = TransportFactory::create(myConfig);

  if (g_buttonInput) g_buttonInput->begin(myConfig);
  g_ledController->begin(myConfig);

  g_localAccess.begin(myConfig);   // AP (always on) + STA (unless Ethernet) + webserver + ws-server
  g_statusUplink->begin(myConfig);
}

void loop() {
  uint32_t now = millis();

  g_localAccess.loop();
  g_statusUplink->loop();
  ESP.wdtFeed();
  yield();

  if (g_buttonInput) {
    ButtonEvent ev;
    if (g_buttonInput->poll(now, ev)) {
      if (ev.type == ButtonEventType::PRESSED) {
        // Instant audible feedback on PRESS, not release — otherwise there's
        // no way to tell a press registered until the finger comes off the
        // button. PALM_ATTACHED is an accessory-presence signal, not a
        // press, so it stays silent (its type defaults to RELEASED anyway).
        // Buzzer keeps beeping while held — see BuzzerController for the
        // long-press-cap / min-duration-floor stop conditions.
        debugdata(String("BUTTON PRESSED: " + String(buttonActionName(ev.action))).c_str());
        if (ev.action != ButtonAction::PALM_ATTACHED) g_buzzer.trigger(now);
        // Long-press vs short-press isn't known yet — the actual call-state
        // action is only applied below, on RELEASED.
      }
      else {
        debugdata(String("BUTTON RELEASED: " + String(buttonActionName(ev.action)) +
                          (ev.isLongPress ? " (long press)" : " (short press)")).c_str());

        // Ask the buzzer to stop — it may already have stopped on its own
        // if this held long enough to hit the long-press cap; if this was a
        // short press, it keeps beeping until the min-duration floor is hit
        // (see BuzzerController::tick()) so a very quick tap is still audible.
        if (ev.action != ButtonAction::PALM_ATTACHED) g_buzzer.release();

        if (ev.action == ButtonAction::TOILET_CALL && !bedToiletShareUnit(myConfig)) {
          // This deliberately bypasses CallStateMachine::apply() entirely:
          // it's a routing decision (whose identity this report belongs
          // to), not a change to this bed's own CallStatus, so
          // g_callStatus/leds_[0] are untouched. Toilet-Call(5) is reserved
          // for the bedToiletShareUnit()==true case below, where there's no
          // separate identity to address and the toilet call folds into
          // this device's own combined report instead.
          sendToiletStatus((uint8_t)CallState::CALL);
        }
        else {
          bool legal = CallStateMachine::apply(
              g_callStatus, ev.action, ev.isLongPress,
              bedToiletShareUnit(myConfig), myConfig.ruleset, myConfig.houseKeepings);

          debugdata(String("STATE: " + String(legal ? "ACCEPTED" : "REJECTED") +
                            " -> mainState=" + String((int)g_callStatus.mainState) +
                            " toilet=" + String(g_callStatus.toiletCallActive ? 1 : 0) +
                            " housekeeping=" + String(g_callStatus.housekeeping ? 1 : 0)).c_str());

          if (legal) sendBedStatus();

          // Clear/Cancel is a whole-room reset: as well as whatever it just
          // did to this bed's own CallStatus above (per the normal ruleset),
          // it also clears the SEPARATE toilet device's status, since the
          // TOILET_CALL branch above never touches g_callStatus for that
          // case and so has no "clear via CANCEL" path of its own otherwise.
          // Unconditional (not gated on `legal`) — the toilet clear isn't
          // subject to this bed's own cancel ruleset restrictions.
          if (ev.action == ButtonAction::CANCEL && !bedToiletShareUnit(myConfig)) {
            sendToiletStatus((uint8_t)CallState::IDLE);
          }
        }
      }
    }
  }

  // Periodic full-status resend, independent of button events — dynamic
  // (myConfig.statusReportIntervalSec, Form 2, validated to a multiple of
  // 10 seconds, min 10) rather than hardcoded. Re-armed by sendBedStatus()/
  // sendToiletStatus() themselves (see g_lastStatusReportMs), so this only
  // actually fires when nothing has been reported for a full interval, not
  // redundantly right after a real button-triggered send. Sends this
  // device's own bed status always; ALSO resends the separate toilet
  // device's last known status when toiletid names a distinct device —
  // when it's the same unit, that state is already folded into the one
  // bed status above, so there's nothing extra to send.
  uint32_t reportIntervalMs =
      (uint32_t)((myConfig.statusReportIntervalSec >= 10) ? myConfig.statusReportIntervalSec : 30) * 1000UL;
  if ((uint32_t)(now - g_lastStatusReportMs) >= reportIntervalMs) {
    debugdata("PERIODIC: status report interval elapsed");
    sendBedStatus();
    if (!bedToiletShareUnit(myConfig)) sendToiletStatus(g_lastToiletStatus);
  }

  g_buzzer.tick(now);
  g_ledController->refreshConfig(myConfig);
  g_ledController->setCallZone(g_callStatus, myConfig.ruleset);
  g_ledController->setAggregateZone(g_roomAggregator.roomStates(), g_roomAggregator.count());
  g_ledController->setToiletRemoteStatus(g_roomAggregator.statusFor((uint16_t)myConfig.toiletid));
  g_ledController->setLinkStatus(g_statusUplink->isNetworkUp(), g_statusUplink->isLinkUp());
  g_ledController->tick(now);
}

// Forward-declared and called from NewMeshNOW.h's uplinkHealthCheck() once
// the uplink comes back up after being down — re-pushes the device's
// current status (and the separate toilet's, if any) so the server isn't
// left with a stale value from before the outage.
void resendCurrentStatus() {
  if (!g_statusUplink) return;
  sendBedStatus();
  if (!bedToiletShareUnit(myConfig)) sendToiletStatus(g_lastToiletStatus);
}

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
#define MQTTNOTREQUIRED
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
        debugdata(String("BUTTON PRESSED: " + String(buttonActionName(ev.action))).c_str());
        if (ev.action != ButtonAction::PALM_ATTACHED) g_buzzer.trigger(now);
        // Long-press vs short-press isn't known yet — the actual call-state
        // action is only applied below, on RELEASED.
      }
      else {
        debugdata(String("BUTTON RELEASED: " + String(buttonActionName(ev.action)) +
                          (ev.isLongPress ? " (long press)" : " (short press)")).c_str());

        bool legal = CallStateMachine::apply(
            g_callStatus, ev.action, ev.isLongPress,
            bedToiletShareUnit(myConfig), myConfig.ruleset, myConfig.houseKeepings);

        debugdata(String("STATE: " + String(legal ? "ACCEPTED" : "REJECTED") +
                          " -> mainState=" + String((int)g_callStatus.mainState) +
                          " toilet=" + String(g_callStatus.toiletCallActive ? 1 : 0) +
                          " housekeeping=" + String(g_callStatus.housekeeping ? 1 : 0)).c_str());

        if (legal) {
          StatusPayload payload;
          payload.deviceId = (uint16_t)myConfig.machineid;
          payload.statusCode = CallStateMachine::reportedStatusCode(g_callStatus, myConfig.ruleset);
          debugdata(String("SEND: statusCode=" + String(payload.statusCode) +
                            " via route=" + String(myConfig.routeType) +
                            " linkUp=" + String(g_statusUplink->isLinkUp() ? 1 : 0)).c_str());
          g_statusUplink->sendStatus(payload);
        }
      }
    }
  }

  g_buzzer.tick(now);
  g_ledController->setCallZone(g_callStatus, myConfig.ruleset);
  g_ledController->setAggregateZone(g_roomAggregator.roomStates(), g_roomAggregator.count());
  g_ledController->setLinkStatus(g_statusUplink->isNetworkUp(), g_statusUplink->isLinkUp());
  g_ledController->tick(now);
}

// Forward-declared and called from NewMeshNOW.h's uplinkHealthCheck() once
// the uplink comes back up after being down — re-pushes the device's
// current status so the server isn't left with a stale value from before
// the outage.
void resendCurrentStatus() {
  if (!g_statusUplink) return;
  StatusPayload payload;
  payload.deviceId = (uint16_t)myConfig.machineid;
  payload.statusCode = CallStateMachine::reportedStatusCode(g_callStatus, myConfig.ruleset);
  g_statusUplink->sendStatus(payload);
}

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

CallStateMachine::CallStatus g_callStatus;
LedAggregator g_roomAggregator;

static LedStripController s_ledStripController;

IButtonInput    *g_buttonInput = nullptr;
ILedController  *g_ledController = nullptr;
ITransport      *g_statusUplink = nullptr;
LocalAccessStack g_localAccess;

void setup() {
  Serial.begin(115200);

  configLoad();
  g_roomAggregator.begin();

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
      bool legal = CallStateMachine::apply(
          g_callStatus, ev.action, ev.isLongPress,
          bedToiletShareUnit(myConfig), myConfig.ruleset, myConfig.houseKeepings);

      if (legal) {
        StatusPayload payload;
        payload.deviceId = (uint16_t)myConfig.machineid;
        payload.statusCode = CallStateMachine::reportedStatusCode(g_callStatus, myConfig.ruleset);
        g_statusUplink->sendStatus(payload);
      }
    }
  }

  g_ledController->setCallZone(g_callStatus, myConfig.ruleset);
  g_ledController->setAggregateZone(g_roomAggregator.roomStates(), g_roomAggregator.count());
  g_ledController->tick(now);
}

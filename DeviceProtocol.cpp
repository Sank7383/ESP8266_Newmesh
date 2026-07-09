#include "DeviceProtocol.h"
#include "NurseCallConfig.h"
#include "MeshNowExports.h"
#include "DeviceState.h"
#include <ESP8266WiFi.h>
#include <string.h>
#include <ctype.h>

// ==========================================
// Restart / OTA stub
// ==========================================

void ESPrestart() {
  // Forces a known-safe pin state before reset, matching the legacy
  // firmware's boot-strapping workaround.
  digitalWrite(0, 1);
  digitalWrite(2, 1);
  digitalWrite(15, 0);
  digitalWrite(1, 1);
  digitalWrite(5, 0);
  delay(100);
  ESP.restart();
}

void handleBinUpdate(int local) {
  // OTA is explicitly out of scope for v1 (see plan) — one firmware image
  // now covers every variant, which makes OTA more valuable than under the
  // old per-variant .bin scheme, so this is flagged as a fast-follow rather
  // than silently dropped.
  debugdata("INFO: OTA update not available in this build");
}

// ==========================================
// Status snapshot
// ==========================================

void getCurrentStatus1(char *buffer, size_t buflen) {
  uint8_t statusCode = CallStateMachine::reportedStatusCode(g_callStatus, myConfig.ruleset);
  String a;
  a.reserve(400);
  a = "Device:" + String(myConfig.myDeviceName) +
      ",ID:" + String(myConfig.machineid) +
      ",Role:" + String(myConfig.deviceRole) +
      ",Route:" + String(myConfig.routeType) +
      ",Status:" + String(statusCode) +
      ",Housekeeping:" + String(g_callStatus.housekeeping ? 1 : 0) +
      ",Connected:" + String(isConnected() ? 1 : 0) +
      ",Heap:" + String(ESP.getFreeHeap()) +
      ",Uptime:" + String(millis() / 1000);
  strncpy(buffer, a.c_str(), buflen - 1);
  buffer[buflen - 1] = 0;
}

// ==========================================
// Nurse-call domain message dispatch
// ==========================================

void decodeString(const char *msm, int isServer) {
  if (!msm) return;
  String s = String(msm);

  // "j<roomId>,<statusCode>" — room status broadcast from a peer device
  // (the same single reported code CallStateMachine::reportedStatusCode()
  // produces), consumed into the multi-room aggregator (LedAggregator),
  // never fed back into this device's own CallStateMachine.
  if (s.length() > 1 && s.charAt(0) == 'j' && isDigit(s.charAt(1))) {
    int c1 = s.indexOf(',');
    if (c1 > 0) {
      uint16_t roomId = (uint16_t)s.substring(1, c1).toInt();
      uint8_t statusCode = (uint8_t)s.substring(c1 + 1).toInt();
      g_roomAggregator.updateRoom(roomId, statusCode);
    }
    return;
  }
}

// ==========================================
// Settings-form protocol
//
// Wire grammar unchanged from the legacy firmware: $NEXT$/$DONE$/$T|/$D|/$R|
// tokens, positional "formid|argk|urlencoded_value" submission. New pages
// (11-13) are appended rather than renumbering any existing formid/argk so
// no companion app needs to change.
// ==========================================

static void appendText(String &msg, const char *label, const String &value) {
  msg += "$T|";
  msg += label;
  msg += "||";
  msg += urlencode(value);
  msg += "|0";
}

static void appendDropdown(String &msg, const char *label, const char *options, int currentValue) {
  msg += "$D|";
  msg += label;
  msg += "|";
  msg += options;
  msg += "|";
  msg += urlencode(String(currentValue));
  msg += "|0";
}

#define BUTTON_ACTION_OPTIONS "None:0#Call:1#Cancel:2#Toilet:3#Extra:4#CodeBlue:5#Housekeeping:6#Palm:7#ApMode:8#Care:9"
#define BUTTON_ACTION_MAX 9

void sendFormData(int formno) {
  String msg;
  msg.reserve(1200);

  if (formno == 0) {
    msg = "FORM:0$NEXT$Select$R|Select Settings|"
          "Network:1#Device:2#LED:3#RS485:4#Mesh:5#Info:7#Role and Route:11#Buttons:12#Ruleset:13#Debug:14#Reboot:20#Factory Reset:21|1|0";
  }
  else if (formno == 1) {
    msg = "FORM:1$DONE$Network Settings";
    appendText(msg, "SSID", myConfig.mySSID);
    appendText(msg, "Password", myConfig.myPass);
    // Which interface myIP/myGateway/myNetmask below apply to, and whether
    // WiFi-STA even attempts mySSID at all — see NetworkType in
    // NurseCallConfig.h. Selecting Ethernet here means SSID/Password are
    // simply ignored (no need to fill them with a placeholder just to
    // "complete" the form) — WiFi-STA join is skipped entirely at boot.
    appendDropdown(msg, "Network Type", "WiFi:0#Ethernet:1", myConfig.networkType);
    appendText(msg, "Static IP (blank = DHCP)", myConfig.myIP);
    appendText(msg, "Gateway", myConfig.myGateway);
    appendText(msg, "Subnet Mask", myConfig.myNetmask);
  }
  else if (formno == 2) {
    msg = "FORM:2$DONE$Device Settings";
    appendText(msg, "Device Name", myConfig.myDeviceName);
    appendText(msg, "Company Code (asccode)", String(myConfig.asccode));
    appendText(msg, "Device ID", String(myConfig.machineid));
    appendText(msg, "Toilet ID", String(myConfig.toiletid));
    appendText(msg, "Door Indicator ID", String(myConfig.doorIndicatorId));
    appendText(msg, "Server IP", myConfig.myServer);
    appendText(msg, "Server Port", String(myConfig.myPort));
    appendDropdown(msg, "Server Type", "WebSocket:0#SocketIO:1", myConfig.socketio ? 1 : 0);
  }
  else if (formno == 3) {
    msg = "FORM:3$DONE$LED Settings";
    appendDropdown(msg, "Color Row", "RGB:0#GRB:1#BRG:2", myConfig.color_row_indi);
    appendText(msg, "LED Brightness", String(myConfig.ledBrightness));
    appendDropdown(msg, "Default LED On", "Off:0#On:1", myConfig.default_led ? 1 : 0);
    appendText(msg, "Active LED Count", String(myConfig.ledCall.count));
    appendText(msg, "Idle Blink Interval Sec (multiple of 5, min 5)", String(myConfig.Indicator_timer));
    appendDropdown(msg, "Toilet Idle Indication", "Off:0#On:1", myConfig.toiletIndicationOnIdle ? 1 : 0);
  }
  else if (formno == 4) {
    msg = "FORM:4$DONE$RS485 Settings";
    appendDropdown(msg, "New 485 Module", "No:0#Yes:1", myConfig.new485Module ? 1 : 0);
    appendText(msg, "Legacy Protocol Select", String(myConfig.newProtocol));
    int baudIdx = (myConfig.rs485Baud == 4800) ? 1 : (myConfig.rs485Baud == 19200) ? 2 : 0;
    appendDropdown(msg, "Baud Rate", "9600:0#4800:1#19200:2", baudIdx);
  }
  else if (formno == 5) {
    msg = "FORM:5$DONE$Mesh Settings";
    appendText(msg, "Node ID", String(myConfig.nodeId));
    appendText(msg, "Mesh ID", String(myConfig.meshId));
    appendText(msg, "WiFi Channel", String(myConfig.wifiChannel));
    appendText(msg, "Tx Time", String(myConfig.txTime));
    appendDropdown(msg, "Mesh Enabled", "No:0#Yes:1", myConfig.mesh_en ? 1 : 0);
    appendText(msg, "Retransmit", String(myConfig.retransmit));
    appendText(msg, "Status Report Interval Sec (multiple of 10, min 10)", String(myConfig.statusReportIntervalSec));
  }
  else if (formno == 7) {
    char status[400];
    getCurrentStatus1(status, sizeof(status));
    msg = "FORM:7$DONE$Info$T|Status||" + urlencode(String(status)) + "|0";
  }
  else if (formno == 11) {
    msg = "FORM:11$DONE$Device Role and Route";
    appendDropdown(msg, "Device Role", "Bed:0#Toilet:1#DoorIndicator:2#Combo:3", myConfig.deviceRole);
    appendDropdown(msg, "Route Type", "MeshWiFi:0#RS485:1#Ethernet:2", myConfig.routeType);
    // General device-behavior toggle, not specific to network/LED/RS485/
    // mesh — grouped here with Role/Route instead. Now actually enforced
    // (see NewMeshNOW.h): gates the unauthenticated/automatic reboot
    // triggers (remote "Reboot"/"Reset" mesh commands, the /reboot HTTP
    // endpoint, and the persistent-uplink-failure auto-restart) — NOT the
    // Reboot/Factory Reset menu options on Form 0, which stay always
    // available as the deliberate settings-UI action.
    appendDropdown(msg, "Allow Reboot", "No:0#Yes:1", myConfig.allowReboot ? 1 : 0);
  }
  else if (formno == 12) {
    msg = "FORM:12$DONE$Button Configuration";
    appendDropdown(msg, "Button Variant", "None:0#Shift5:1#Shift3:2#Gpio2:3#Gpio3Remote:4", myConfig.buttonVariant);
    appendDropdown(msg, "PCB Revision", "Old:0#NewSticker:1", myConfig.buttonPcbRevision);
    static const char *slotLabels[7] = { "Slot Cancel", "Slot Call", "Slot Toilet", "Slot Extra", "Slot Blue", "Slot Attend", "Slot AP" };
    for (uint8_t i = 0; i < 7; i++) {
      appendDropdown(msg, slotLabels[i], BUTTON_ACTION_OPTIONS, (int)myConfig.buttonMap.slot[i]);
    }
  }
  else if (formno == 13) {
    msg = "FORM:13$DONE$Call Ruleset";
    appendDropdown(msg, "Ruleset Preset", "Housekeeping:0#LegacyRS485:1#Custom:2", myConfig.ruleset.preset);
    appendDropdown(msg, "Housekeeping Bypasses Care", "No:0#Yes:1", myConfig.houseKeepings ? 1 : 0);
    appendDropdown(msg, "Care Required Before Cancel", "No:0#Yes:1", myConfig.ruleset.careRequiredBeforeCancel ? 1 : 0);
    appendDropdown(msg, "Direct Code Blue From Idle", "No:0#Yes:1", myConfig.ruleset.directCodeBlueFromIdle ? 1 : 0);
    appendDropdown(msg, "Custom State Enabled", "No:0#Yes:1", myConfig.ruleset.customState.enabled ? 1 : 0);
    appendText(msg, "Custom State Reported Code", String(myConfig.ruleset.customState.reportedCode));
    appendText(msg, "Custom State LED Color Index (0-7)", String(myConfig.ruleset.customState.ledColorIndex));
  }
  else if (formno == 14) {
    // Read-only diagnostics, deliberately ONE compact text blob (not a
    // page of separate boxes) so it's readable at a glance — everything
    // read live, nothing cached. Includes the internal uplink/WebSocket
    // client state (is it even trying to connect, has it ever succeeded,
    // how many times has it dropped) plus the LED/call-state inputs, so
    // "not connecting to server" and "LED color not changing" are both
    // answerable from this one page instead of guessing.
    bool staConnected = (WiFi.status() == WL_CONNECTED);
    String uplinkTarget = strlen(myConfig.myServer) > 0
        ? String(myConfig.myServer) + ":" + String(myConfig.myPort) + (myConfig.socketio ? " (SocketIO)" : " (WebSocket)")
        : String("(myServer is empty - uplink never attempts to connect)");
    bool uplinkUp = myConfig.socketio ? webSocketIo.isConnected() : webSocketClient.isConnected();
    String secsSinceUplinkOk = myConfig.socketio
        ? ((lst_con == 0) ? String("never") : String((millis() - lst_con) / 1000) + "s ago")
        : ((lst_wscon == 0) ? String("never") : String((millis() - lst_wscon) / 1000) + "s ago");

    // Real newlines — urlencode(String) now correctly percent-encodes
    // control characters as %0A instead of dropping them (see NewMeshNOW.h),
    // so this round-trips through decodeURIComponent() on the client as
    // actual line breaks in the Debug textarea.
    const char *SEP = "\n";
    String d;
    d.reserve(400);
    d += "WiFi:" + (staConnected ? WiFi.SSID() : String("none")) +
         (staConnected ? ("(" + String(WiFi.RSSI()) + "dBm)") : "") +
         " STA:" + WiFi.localIP().toString() + " GW:" + WiFi.gatewayIP().toString() + SEP;
    d += "AP:" + WiFi.softAPSSID() + "@" + WiFi.softAPIP().toString() +
         " clients:" + String(WiFi.softAPgetStationNum()) +
         " role:" + String(isConnected() ? "bridged" : "standalone") + SEP;
    d += "Uplink target:" + uplinkTarget +
         " connected:" + String(uplinkUp ? 1 : 0) +
         " lastOK:" + secsSinceUplinkOk +
         " failCount:" + String(uplinkFailCount) +
         " reconnects:" + String(wscdisconnect) + SEP;
    d += "Route:" + String(myConfig.routeType) + " amServer:" + String(amServer ? 1 : 0) +
         " eth:" + String(ethercon) + " espnow:" + String(meshLayerActive() ? 1 : 0) + SEP;
    d += "CallState:" + String((int)g_callStatus.mainState) +
         " Toilet:" + String(g_callStatus.toiletCallActive ? 1 : 0) +
         " Housekeeping:" + String(g_callStatus.housekeeping ? 1 : 0) +
         " Role:" + String(myConfig.deviceRole) +
         " (LED call-color only shows for Role 0/1/3 - Role 2=DoorIndicator only shows aggregate color)" + SEP;
    d += "LED link status: networkUp:" + String(g_statusUplink->isNetworkUp() ? 1 : 0) +
         " serverUp:" + String(g_statusUplink->isLinkUp() ? 1 : 0) +
         " (networkUp=0 blinks white, serverUp=0 blinks pink - either overrides the call color)" + SEP;
    d += "Heap:" + String(ESP.getFreeHeap()) + " Up:" + String(millis() / 1000) + "s";

    msg = "FORM:14$DONE$Debug$T|Debug||" + urlencode(d) + "|0";
  }
  else {
    return;
  }

  sendToAll(msg.c_str(), 0);
}

void getFormData(const char *formdata1, int socketnumber) {
  String formdata = String(formdata1);
  if (formdata.startsWith("RESPONSE:")) formdata = formdata.substring(9);

  char buf[700];
  strncpy(buf, formdata.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  int formid = -1;
  char *command = strtok(buf, " ");
  while (command != 0) {
    char *separator = strchr(command, '|');
    if (separator != 0) {
      *separator = 0;
      int argk = atoi(command);
      ++separator;
      String argv = urldecode(String(separator));

      if (formid == -1) {
        formid = argk;
      }
      else if (formid == 0) {
        if (argk == 0) {
          int sel = argv.toInt();
          if (sel >= 1 && sel <= 5) { sendFormData(sel); return; }
          if (sel == 7 || sel == 11 || sel == 12 || sel == 13 || sel == 14) { sendFormData(sel); return; }
          if (sel == 20) { ESPrestart(); return; }
          if (sel == 21) { configApplyDefaults(myConfig); configSave(); ESPrestart(); return; }
        }
      }
      else if (formid == 1) {
        if (argk == 0) copyString(myConfig.mySSID, argv, sizeof(myConfig.mySSID));
        else if (argk == 1) copyString(myConfig.myPass, argv, sizeof(myConfig.myPass));
        else if (argk == 2) myConfig.networkType = clampEnum(argv.toInt(), 1);
        else if (argk == 3) copyString(myConfig.myIP, argv, sizeof(myConfig.myIP));
        else if (argk == 4) copyString(myConfig.myGateway, argv, sizeof(myConfig.myGateway));
        else if (argk == 5) copyString(myConfig.myNetmask, argv, sizeof(myConfig.myNetmask));
      }
      else if (formid == 2) {
        if (argk == 0) copyString(myConfig.myDeviceName, argv, sizeof(myConfig.myDeviceName));
        else if (argk == 1) myConfig.asccode = argv.toInt();
        else if (argk == 2) myConfig.machineid = argv.toInt();
        else if (argk == 3) myConfig.toiletid = argv.toInt();
        else if (argk == 4) myConfig.doorIndicatorId = (uint32_t)argv.toInt();
        else if (argk == 5) copyString(myConfig.myServer, argv, sizeof(myConfig.myServer));
        else if (argk == 6) myConfig.myPort = argv.toInt();
        else if (argk == 7) myConfig.socketio = (clampEnum(argv.toInt(), 1) == 1);
      }
      else if (formid == 3) {
        if (argk == 0) {
          myConfig.color_row_indi = clampEnum(argv.toInt(), 2);
          myConfig.color_row_frnt = myConfig.color_row_indi;
          myConfig.color_row_toi = myConfig.color_row_indi;
        }
        else if (argk == 1) myConfig.ledBrightness = (uint8_t)constrain(argv.toInt(), 0, 255);
        else if (argk == 2) myConfig.default_led = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 3) {
          uint8_t count = (uint8_t)constrain(argv.toInt(), 1, MAX_LEDS);
          myConfig.ledCall.count = count;
          myConfig.ledToilet.count = count;
          myConfig.ledAggregate.count = count;
        }
        else if (argk == 4) {
          long secs = argv.toInt();
          if (secs < 5) secs = 5;
          secs = ((secs + 2) / 5) * 5;   // round to nearest multiple of 5
          myConfig.Indicator_timer = (int)secs;
        }
        else if (argk == 5) myConfig.toiletIndicationOnIdle = (clampEnum(argv.toInt(), 1) == 1);
      }
      else if (formid == 4) {
        if (argk == 0) myConfig.new485Module = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 1) myConfig.newProtocol = (uint8_t)constrain(argv.toInt(), 0, 5);
        else if (argk == 2) {
          static const uint32_t bauds[3] = { 9600, 4800, 19200 };
          myConfig.rs485Baud = bauds[clampEnum(argv.toInt(), 2)];
        }
      }
      else if (formid == 5) {
        if (argk == 0) myConfig.nodeId = (uint32_t)argv.toInt();
        else if (argk == 1) myConfig.meshId = (uint16_t)argv.toInt();
        else if (argk == 2) myConfig.wifiChannel = (uint8_t)constrain(argv.toInt(), 1, 13);
        else if (argk == 3) myConfig.txTime = (uint16_t)constrain(argv.toInt(), 2000, 65000);
        else if (argk == 4) myConfig.mesh_en = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 5) myConfig.retransmit = (uint8_t)argv.toInt();
        else if (argk == 6) {
          long secs = argv.toInt();
          if (secs < 10) secs = 10;
          secs = ((secs + 5) / 10) * 10;   // round to nearest multiple of 10
          myConfig.statusReportIntervalSec = (uint32_t)secs;
        }
      }
      else if (formid == 11) {
        if (argk == 0) myConfig.deviceRole = clampEnum(argv.toInt(), 3);
        else if (argk == 1) myConfig.routeType = clampEnum(argv.toInt(), 2);
        else if (argk == 2) myConfig.allowReboot = (clampEnum(argv.toInt(), 1) == 1);
      }
      else if (formid == 12) {
        if (argk == 0) myConfig.buttonVariant = clampEnum(argv.toInt(), 4);
        else if (argk == 1) myConfig.buttonPcbRevision = clampEnum(argv.toInt(), 1);
        else if (argk >= 2 && argk <= 8) {
          uint8_t slot = argk - 2;
          myConfig.buttonMap.slot[slot] = (ButtonAction)clampEnum(argv.toInt(), BUTTON_ACTION_MAX);
        }
      }
      else if (formid == 13) {
        if (argk == 0) myConfig.ruleset.preset = clampEnum(argv.toInt(), 2);
        else if (argk == 1) myConfig.houseKeepings = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 2) myConfig.ruleset.careRequiredBeforeCancel = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 3) myConfig.ruleset.directCodeBlueFromIdle = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 4) myConfig.ruleset.customState.enabled = (clampEnum(argv.toInt(), 1) == 1);
        else if (argk == 5) myConfig.ruleset.customState.reportedCode = (uint8_t)constrain(argv.toInt(), 0, 255);
        else if (argk == 6) myConfig.ruleset.customState.ledColorIndex = (uint8_t)constrain(argv.toInt(), 0, 7);
      }
    }
    command = strtok(NULL, " ");
  }

  if (formid == 14) {
    // Debug is a read-only diagnostics view — nothing to persist, just
    // re-send the (freshly-read-live) page if the client re-submits it.
    sendFormData(14);
    return;
  }

  if (formid > 0) {
    configSave();
    sendFormData(0);
  }
}

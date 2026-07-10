#pragma once
#include <Arduino.h>

// ==========================================
// Runtime-variant configuration for the nurse-call firmware.
//
// This struct is the single persisted source of truth for which hardware
// and behavior variant a given flashed unit is acting as. Legacy fields are
// kept in their original order/type so an EEPROM blob written by earlier
// builds of this rewrite stays layout-compatible; NEW fields are always
// appended at the end. See structVersion below.
// ==========================================

#define MAX_LEDS  25
#define MAX_ROOMS 100

enum class RouteType : uint8_t { MESH_WIFI = 0, RS485 = 1, ETHERNET = 2 };

// Which physical network interface is the PRIORITY IP interface —
// authoritative regardless of RouteType (Form 11): selecting ETHERNET here
// brings up the ENC28J60 and tries it first even if Route Type is still set
// to MeshWiFi, and vice versa. RouteType now only distinguishes RS485 (its
// own bus, entirely separate) from "IP network" — for anything other than
// RS485, networkType is what actually decides WiFi vs Ethernet, via
// TransportFactory -> TransportNetworkPriority. Governs: (1) which
// interface's hardware is brought up first at boot (WiFi-STA join of
// mySSID, or TransportEthernet's deferred ENC28J60 bring-up), and (2) which
// interface myConfig.myIP/myGateway/myNetmask apply to — the other
// interface always falls back to DHCP if it ends up used via fallback
// below. See allowNetworkFallback/networkFailoverSec for what happens if
// the priority interface doesn't come up in time.
enum class NetworkType : uint8_t { WIFI = 0, ETHERNET = 1 };

enum class ButtonVariant : uint8_t {
  NONE = 0,             // door indicator, no buttons
  SHIFTREG_5BTN = 1,    // 74HC165, full 5-button + palm
  SHIFTREG_3BTN = 2,    // same read path, 4 of 7 slots remapped to NONE via buttonMap
  GPIO_2BTN = 3,
  GPIO_3BTN_RMT = 4
};

enum class ButtonPcbRevision : uint8_t { OLD_PCB = 0, NEW_STICKER = 1 };

enum class UplinkProtocol : uint8_t { WEBSOCKET = 0, SOCKET_IO = 1 };

enum class DeviceRole : uint8_t { BED_UNIT = 0, TOILET_PULLCORD = 1, DOOR_INDICATOR = 2, BED_TOILET_COMBO = 3 };

enum class ButtonAction : uint8_t {
  NONE = 0, CALL, CANCEL, TOILET_CALL, EXTRA_HELP, CODE_BLUE, HOUSEKEEPING, PALM_ATTACHED, AP_MODE,
  CARE   // "attend"/acknowledge button — appended at the end so existing stored
         // ButtonActionMap values (0-8) already in the field keep their meaning
};

enum class RulesetPreset : uint8_t { HOUSEKEEPING_DEFAULT = 0, LEGACY_RS485 = 1, CUSTOM = 2 };

// Human-readable name for live trace logging (debugdata()) — not used for
// anything protocol-facing.
inline const char* buttonActionName(ButtonAction a) {
  switch (a) {
    case ButtonAction::CALL: return "CALL";
    case ButtonAction::CANCEL: return "CANCEL";
    case ButtonAction::TOILET_CALL: return "TOILET_CALL";
    case ButtonAction::EXTRA_HELP: return "EXTRA_HELP";
    case ButtonAction::CODE_BLUE: return "CODE_BLUE";
    case ButtonAction::HOUSEKEEPING: return "HOUSEKEEPING";
    case ButtonAction::PALM_ATTACHED: return "PALM_ATTACHED";
    case ButtonAction::AP_MODE: return "AP_MODE";
    case ButtonAction::CARE: return "CARE";
    default: return "NONE";
  }
}

// Fixed slot order matches the legacy HC165 button_array[7] table:
// {cancel, call, toi, extra, blue, attend, ap}
struct ButtonActionMap {
  ButtonAction slot[7];
};

// The device's call status, reported to the server as a plain number —
// these are the SAME numeric codes your server already understands (see
// button.h): 0 idle, 1 call/red, 2 care/green, 3 extra-help/orange,
// 4 code-blue/blue, 5 toilet-call/red, 7 housekeeping/pink,
// 8 housekeeping+call combo. Nothing here is bit-packed — a device is
// always in exactly ONE of these named states (housekeeping is tracked as
// a separate on/off flag, not folded into this number — see CallStatus
// below for how the two combine into the single reported code).
//
// CUSTOM (6) is a worked example of how to add a site-specific state: it
// already has a name here, a `case` in CallStateMachine::apply()'s switch,
// a reported code + LED color driven by CustomStateSlot (below), and a
// display color in LedStripController. To add a SECOND custom state, copy
// that same pattern end-to-end.
enum class CallState : uint8_t {
  IDLE = 0,
  CALL = 1,
  CARE = 2,          // staff has acknowledged/is attending — the "attend" button
  EXTRA_HELP = 3,
  CODE_BLUE = 4,
  TOILET_CALL = 5,
  CUSTOM = 6,        // worked example of a site-specific extra state, see CustomStateSlot below
  HOUSEKEEPING = 7,             // reported only when idle + housekeeping flag set
  HOUSEKEEPING_CALL = 8,        // reported when housekeeping flag set + any active call/care/help/blue/toilet-call
};

// A single, ready-to-use extension point for a site-specific state beyond
// the standard ones above (CallState::CUSTOM). To add a SECOND custom
// state, copy this pattern: add CallState::CUSTOM_2, a second
// CustomStateSlot field here, a case in CallStateMachine, and a color in
// LedStripController.
struct CustomStateSlot {
  bool    enabled;
  uint8_t reportedCode;      // the number sent to the server for this state (default 6)
  uint8_t ledColorIndex;     // which color table column to paint (see LedStripController)
};

struct CallRulesetConfig {
  uint8_t preset;                     // RulesetPreset
  bool    careRequiredBeforeCancel;   // true: CALL/TOILET_CALL cannot be cancelled directly, only via CARE
                                       // (the legacy "5-button care and clear rule")
  bool    directCodeBlueFromIdle;     // == legacy myConfig.direct_code_blue: allow CODE_BLUE straight from IDLE
  CustomStateSlot customState;
  // Note: whether EXTRA_HELP/CODE_BLUE are reachable directly from CALL/
  // TOILET_CALL (bypassing CARE) is governed by the existing top-level
  // myConfig.houseKeepings flag, matching button.h's `houseKeepings==1`
  // check — not duplicated here.
};

struct LedZoneConfig {
  uint8_t pin;
  uint8_t count;
  uint8_t colorRow;      // 0=RGB, 1=GRB, 2=BRG
  uint8_t brightness;
};

// mySchedule kept for EEPROM offset compatibility with earlier field builds;
// not used by the runtime-variant logic in this rewrite.
struct mySchedule {
  int16_t hourminstart;
  int16_t hourminend;
  byte weekday;
  byte channel;
};

struct DeviceConfig {
  uint32_t structVersion;   // bump on layout/semantic change; mismatch => configApplyDefaults()

  // ---- legacy fields, kept verbatim in this order (EEPROM offset compatibility) ----
  char mySSID[20];
  char myPass[20];
  char myIP[16];
  char myNetmask[16];
  char myGateway[16];
  char myLocalServer[16];
  char myDeviceName[20];
  char names[1][20];
  char Fix;
  mySchedule schedule[10];
  byte type[8];
  byte fansp[8];
  int timer[8];
  boolean offline;
  char myServer[20];
  int myPort;
  char myDeviceIdentification;
  uint32_t doorIndicatorId;
  int asccode;
  int machineid;
  int toiletid;
  byte color_row_indi;    // 0:RGB, 1:GRB, 2:BRG
  byte color_row_frnt;
  byte color_row_toi;
  boolean socketio;        // canonical storage; UplinkProtocol is a typed accessor over this bit
  int Indicator_timer;
  boolean default_led;
  boolean direct_code_blue;
  bool allowReboot;
  uint8_t ledBrightness;
  boolean new485Module;
  uint8_t newProtocol;

  uint32_t nodeId;
  uint32_t targetId;
  uint8_t  wifiChannel;
  uint16_t meshId;
  uint16_t txTime;
  bool     houseKeepings;
  uint8_t  retransmit;
  bool     mesh_en;

  // ---- new fields, appended only ----
  uint8_t           routeType;          // RouteType
  uint8_t           buttonVariant;      // ButtonVariant
  uint8_t           buttonPcbRevision;  // ButtonPcbRevision
  uint8_t           deviceRole;         // DeviceRole
  ButtonActionMap   buttonMap;
  CallRulesetConfig ruleset;
  uint32_t          rs485Baud;          // 9600 / 4800 / 19200
  LedZoneConfig     ledCall;
  LedZoneConfig     ledToilet;
  LedZoneConfig     ledAggregate;
  // Whether leds_[1] (the toilet zone) shows idleColorRaw() (steady/heartbeat
  // per Default LED On) when the toilet has no active call, or just stays
  // fully off regardless of Default LED On. Only affects the no-call idle
  // display — an actual active toilet call always shows regardless of this.
  bool              toiletIndicationOnIdle;
  // Periodic full-status resend to the server, independent of button
  // events — whole seconds, validated/rounded to a multiple of 10 (min 10)
  // in DeviceProtocol.cpp's Form 2 parser. See the .ino's loop() for where
  // this drives the actual resend.
  uint32_t          statusReportIntervalSec;
  // See NetworkType above — Form 1, authoritative over routeType for WiFi
  // vs Ethernet selection.
  uint8_t           networkType;
  // If the priority interface (networkType) hasn't reached isNetworkUp()
  // within networkFailoverSec of boot, TransportNetworkPriority brings up
  // the OTHER interface's hardware as a fallback — Form 1, "Allow Network
  // Fallback" / "Network Failover Sec". Once a fallback interface links, it
  // stays active for the rest of this boot (no continuous re-racing).
  bool              allowNetworkFallback;
  uint16_t          networkFailoverSec;   // validated to >= 10 in DeviceProtocol.cpp's Form 1 parser
};

extern DeviceConfig myConfig;

void configApplyDefaults(DeviceConfig &c);
bool configLoad();   // EEPROM.begin(4096); EEPROM.get(100,myConfig); structVersion check -> defaults on mismatch
void configSave();   // EEPROM.put(100,myConfig); EEPROM.commit();

// bed_toi_common is DERIVED, not stored, matching the legacy behavior.
// toiletid==0 means "no separate toilet unit configured" (the toilet pull
// cord, if any, is this same device's own) — same outcome as toiletid
// equalling this device's own machineid, so both count as "shared".
inline bool bedToiletShareUnit(const DeviceConfig &c) { return c.toiletid == 0 || c.toiletid == c.machineid; }

// Typed, validating accessors used by the settings-form parser (DeviceProtocol.cpp)
// to keep out-of-range values from ever reaching array indices (button_array[],
// led_colour[]) elsewhere in the firmware.
uint8_t clampEnum(int rawValue, uint8_t maxValidInclusive);

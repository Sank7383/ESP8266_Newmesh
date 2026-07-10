#include "NurseCallConfig.h"
#include <EEPROM.h>

// Bump this on ANY change to DeviceConfig's layout or field semantics, even
// an append-only one — configLoad()'s mismatch check is the ONLY thing that
// resets a device's EEPROM to sane defaults. Fields appended after a unit's
// last flash-at-that-version keep whatever garbage bytes were already
// sitting at that EEPROM offset (e.g. ledCall/ledToilet/ledAggregate,
// ruleset, buttonMap were all added after version 1 first shipped) — that
// garbage can silently produce "nothing behaves like it should" symptoms
// (wrong LED strip length, bogus ruleset thresholds) that look like logic
// bugs but are actually stale EEPROM. Bumped 1 -> 2 here to force one clean
// reset onto the current layout; every unit will need its settings redone
// once after this flashes. Bumped 2 -> 3 for the added toiletIndicationOnIdle
// field. Bumped 3 -> 4 for the added statusReportIntervalSec field. Bumped
// 4 -> 5 for the added networkType field. Bumped 5 -> 6 for the added
// allowNetworkFallback/networkFailoverSec fields.
#define CONFIG_STRUCT_VERSION 6u
#define CONFIG_EEPROM_OFFSET 100

DeviceConfig myConfig;

uint8_t clampEnum(int rawValue, uint8_t maxValidInclusive) {
  if (rawValue < 0) return 0;
  if (rawValue > (int)maxValidInclusive) return maxValidInclusive;
  return (uint8_t)rawValue;
}

void configApplyDefaults(DeviceConfig &c) {
  memset(&c, 0, sizeof(DeviceConfig));

  c.structVersion = CONFIG_STRUCT_VERSION;

  strlcpy(c.mySSID, "", sizeof(c.mySSID));
  strlcpy(c.myPass, "", sizeof(c.myPass));
  strlcpy(c.myDeviceName, "NURSE", sizeof(c.myDeviceName));
  c.Fix = 21;

  c.offline = false;
  strlcpy(c.myServer, "", sizeof(c.myServer));
  c.myPort = 80;
  c.myDeviceIdentification = 'T';
  // machineid/toiletid/doorIndicatorId default to the same placeholder (51)
  // so a fresh device is immediately self-consistent out of the box:
  // toiletid==machineid makes bedToiletShareUnit() true by default (no
  // separate toilet device assumed until configured otherwise), and
  // doorIndicatorId matching gives a sane non-zero default for the "doorid"
  // field in outgoing status messages. Every site still reassigns these via
  // Form 2 for a real deployment.
  c.doorIndicatorId = 51;

  c.asccode = 1;
  c.machineid = 51;
  c.toiletid = 51;

  // Defaults to GRB (1), not RGB (0): the vast majority of WS2812/WS2812B
  // strips in the field are natively GRB-ordered — true-RGB-native strips
  // are the rare exception. Since FastLED is always told "RGB" (no
  // reordering, see LedStripController.h), leaving this at row 0 sends
  // colors as if the strip were RGB-native, which on real GRB hardware
  // shows every color with its R and G channels swapped (e.g. CALL_RED
  // shows green, CARE_GREEN shows red). Sites with a genuinely RGB-native
  // strip should switch this to 0 via the settings form (Color Row field).
  c.color_row_indi = 1;
  c.color_row_frnt = 1;
  c.color_row_toi = 1;
  c.socketio = false;
  // Whole seconds (NOT the legacy firmware's *10000ms-per-unit scaling) —
  // how often the idle LED heartbeat-pulses while Default LED On is Off.
  // Validated/rounded to a multiple of 5 (min 5) in DeviceProtocol.cpp's
  // Form 3 parser.
  c.Indicator_timer = 10;
  c.default_led = false;
  c.direct_code_blue = false;
  c.allowReboot = true;
  c.ledBrightness = 80;
  c.new485Module = false;
  c.newProtocol = 0;

  c.nodeId = 1;
  c.targetId = 1;
  c.wifiChannel = 1;
  c.meshId = 1001;
  c.txTime = 10000;
  c.houseKeepings = true;
  c.retransmit = 1;
  c.mesh_en = true;

  c.routeType = (uint8_t)RouteType::MESH_WIFI;
  c.buttonVariant = (uint8_t)ButtonVariant::SHIFTREG_5BTN;
  c.buttonPcbRevision = (uint8_t)ButtonPcbRevision::NEW_STICKER;
  c.deviceRole = (uint8_t)DeviceRole::BED_UNIT;

  // Default 1:1 mapping onto the legacy button_array slot order:
  // {cancel, call, toi, extra, blue, attend, ap} — "attend" defaults to CARE
  // (the acknowledge/attend button) and "ap" defaults to HOUSEKEEPING, since
  // AP-mode entry is historically a button COMBO (hold blue+extra together)
  // rather than its own dedicated wire on most PCBs. Both are just defaults
  // — every slot is independently reassignable via the settings form
  // (formid 12) if a given site's hardware differs.
  c.buttonMap.slot[0] = ButtonAction::CANCEL;
  c.buttonMap.slot[1] = ButtonAction::CALL;
  c.buttonMap.slot[2] = ButtonAction::TOILET_CALL;
  c.buttonMap.slot[3] = ButtonAction::EXTRA_HELP;
  c.buttonMap.slot[4] = ButtonAction::CODE_BLUE;
  c.buttonMap.slot[5] = ButtonAction::CARE;
  c.buttonMap.slot[6] = ButtonAction::HOUSEKEEPING;

  c.ruleset.preset = (uint8_t)RulesetPreset::HOUSEKEEPING_DEFAULT;
  c.ruleset.careRequiredBeforeCancel = true;    // matches button.h: CALL/TOILET_CALL can't be cancelled directly
  c.ruleset.directCodeBlueFromIdle = false;
  c.ruleset.customState.enabled = false;
  c.ruleset.customState.reportedCode = (uint8_t)CallState::CUSTOM;
  c.ruleset.customState.ledColorIndex = 5;   // magenta by default, see LedStripController

  c.rs485Baud = 9600;

  c.ledCall = { 0, 8, 0, 80 };       // pin, count, colorRow, brightness
  c.ledToilet = { 0, 8, 0, 80 };     // shares the strip with ledCall in bed-unit layout
  c.ledAggregate = { 0, 8, 0, 80 };

  c.toiletIndicationOnIdle = true;
  c.statusReportIntervalSec = 30;
  c.networkType = (uint8_t)NetworkType::WIFI;
  c.allowNetworkFallback = true;
  c.networkFailoverSec = 30;
}

bool configLoad() {
  EEPROM.begin(4096);
  EEPROM.get(CONFIG_EEPROM_OFFSET, myConfig);

  if (myConfig.structVersion != CONFIG_STRUCT_VERSION || myConfig.Fix != 21) {
    configApplyDefaults(myConfig);
    EEPROM.put(CONFIG_EEPROM_OFFSET, myConfig);
    EEPROM.commit();
    return false;
  }
  if (myConfig.machineid < 0) myConfig.machineid = 0;

  // Defense-in-depth beyond the structVersion gate above: a stray bit-flip
  // or partial EEPROM write could otherwise leave routeType/buttonVariant
  // holding a value with no corresponding case in ButtonInputFactory/
  // TransportFactory. Both factories already fail safe (nullptr / mesh
  // fallback) rather than crash, but the intended, always-reachable-via-
  // settings-form default variant is WiFi mesh + 5-button — fall back to
  // that explicitly rather than leaving a device stuck with no buttons.
  if (myConfig.routeType > (uint8_t)RouteType::ETHERNET) {
    myConfig.routeType = (uint8_t)RouteType::MESH_WIFI;
  }
  if (myConfig.buttonVariant > (uint8_t)ButtonVariant::GPIO_3BTN_RMT) {
    myConfig.buttonVariant = (uint8_t)ButtonVariant::SHIFTREG_5BTN;
  }

  return true;
}

void configSave() {
  EEPROM.put(CONFIG_EEPROM_OFFSET, myConfig);
  EEPROM.commit();
}

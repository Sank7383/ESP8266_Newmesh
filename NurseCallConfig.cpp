#include "NurseCallConfig.h"
#include <EEPROM.h>

#define CONFIG_STRUCT_VERSION 1u
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
  c.doorIndicatorId = 0;

  c.asccode = 1;
  c.machineid = 1;
  c.toiletid = 0;

  c.color_row_indi = 0;
  c.color_row_frnt = 0;
  c.color_row_toi = 0;
  c.socketio = false;
  c.Indicator_timer = 5;
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
  return true;
}

void configSave() {
  EEPROM.put(CONFIG_EEPROM_OFFSET, myConfig);
  EEPROM.commit();
}

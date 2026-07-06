#pragma once
#include <Arduino.h>

// Implements the forward-declared, device-specific hooks that NewMeshNOW.h
// expects the sketch to provide: the settings-form-over-websocket protocol
// (sendFormData/getFormData, unchanged $NEXT$/$DONE$/$T|/$D|/$R| grammar,
// extended with new formids 11-13 for route/button/ruleset configuration),
// the nurse-call domain message dispatch (decodeString), a status snapshot
// (getCurrentStatus1), a restart helper (ESPrestart), and an OTA stub
// (handleBinUpdate — OTA itself is out of scope for v1, see plan).
void ESPrestart();
void handleBinUpdate(int local);
void getCurrentStatus1(char *buffer, size_t buflen);
void decodeString(const char *msm, int isServer);
void sendFormData(int formno);
void getFormData(const char *formdata, int socketnumber);

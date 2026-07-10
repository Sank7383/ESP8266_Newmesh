#pragma once
#include <Arduino.h>

// Implements the forward-declared, device-specific hooks that NewMeshNOW.h
// expects the sketch to provide: the settings-form-over-websocket protocol
// (sendFormData/getFormData, unchanged $NEXT$/$DONE$/$T|/$D|/$R| grammar,
// extended with new formids 11-13 for route/button/ruleset configuration),
// the nurse-call domain message dispatch (decodeString), a status snapshot
// (getCurrentStatus1), a restart helper (ESPrestart), and OTA (two modes,
// see handleFixedUpdate/handleNamedUpdate below).
void ESPrestart();
// "tupdate" — fixed path AND fixed target on the legacy production update
// endpoint (host + project.php d=/p= params kept verbatim from the
// researched legacy firmware, see DeviceProtocol.cpp's OTA_BASE_URL),
// always the same URL, no argument needed.
void handleFixedUpdate();
// "normal" update — caller supplies a name that's appended as "&t=<name>"
// to that same base URL (repurposing the legacy "&t=<variant-suffix>"
// param — moot now that one image covers every variant — to select an
// arbitrary named build instead). Both modes are gated on
// myConfig.allowReboot (Form 11) — a successful OTA reflashes the device,
// which is at least as sensitive as the remote reboot that flag already
// locks down.
void handleNamedUpdate(const String &filename);
void getCurrentStatus1(char *buffer, size_t buflen);
void decodeString(const char *msm, int isServer);
void sendFormData(int formno);
void getFormData(const char *formdata, int socketnumber);

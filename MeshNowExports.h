#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>

// Extern-only glue header. NewMeshNOW.h / MYESPNOW.h are "single translation
// unit" style headers (full function bodies + global variable definitions,
// no include guards against multi-TU inclusion) — they must be #include'd
// from exactly one .cpp (ESP8266_Newmesh.ino) to avoid duplicate-symbol
// link errors. Every OTHER .cpp file that needs to call into the mesh/
// webserver/websocket layer includes THIS header instead, which only
// declares (never defines) the symbols it needs.
extern ESP8266WebServer server;
extern WebSocketsServer webSocket;
extern WebSocketsClient webSocketClient;
extern String ID;
extern bool amServer;
extern int ethercon;
extern unsigned long nowmillis;
extern uint8_t espnowaval;
extern bool esp8266_now_active;
extern IPAddress apIP;
extern int wscdisconnect;       // consecutive uplink-reconnect count, see webSocketEventClient
extern unsigned long lst_wscon; // millis() timestamp of the last successful uplink connect (0 = never)

bool AmServer();
bool isConnected();
void setup_AP(bool forceRestart = false);
void startconnection();
void myrun();
void sendToAll(const char *msg, int isServer);
void sendMeshMessage(uint16_t destId, String payload, uint8_t type = 0, uint8_t ttl = 5);
void espnow_setup();
void espnow_loop();
void processIncomingPackets(int iamConnected);
void decodeString2(const char *msm, int isServer);
void debugdata(const char *buf);
bool meshLayerActive();
const char *urlencode(const char *str);
String urlencode(String str);
String urldecode(String str);
void copyString(char array[], String s, int sizearray);
// ESPrestart() is the one exception to this header's "declares what
// NewMeshNOW.h/MYESPNOW.h define" rule: NewMeshNOW.h only forward-declares
// it (as a hook it calls), the real definition lives in DeviceProtocol.cpp.
void ESPrestart();

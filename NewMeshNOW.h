
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <WebSocketsClient.h>
#include <Hash.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
void ESPrestart();
byte ESPN[5] = {0,0,0,0,0};
#define NewMeshLib
unsigned long nowmillis=0,ethchecknow=0;
bool manulalAP=false;
void setup_AP(bool forceRestart = false);
#ifndef MQTTNOTREQUIRED
#include <PubSubClient.h>
void Mysubscribe(int id, int unsub = 0);
#endif
#include <time.h>
#include <TimeLib.h>
#define QUOTE(...) F(#__VA_ARGS__)
#ifdef NURSECALLING
#define MESHNETWORK "MESH"
#pragma message "NNETWORK set"
#else
#define MESHNETWORK "MESH"
#endif
#ifdef ether86
#define USING_ENC28J60      true
#include <SPI.h>
#define CSPIN       15      // 5
#include <ENC28J60lwIP.h>
#define SHIELD_TYPE       "ESP8266_ENC28J60 Ethernet"
ENC28J60lwIP eth(CSPIN);
#include <ESP8266WiFi.h>
#endif
#ifdef ether86
using TCPClient = WiFiClient;
#endif
int ethercon=0;

#ifndef MQTTNOTREQUIRED
const char mqtt_server[4][20] = {"27.54.182.52", "116.72.19.155", "123.201.110.114", "www.ask4token.com"};
WiFiClient espClient;

PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (500)
char msg[MSG_BUFFER_SIZE];
#endif
uint8_t gettime = 0;
// Espalexa/Alexa integration intentionally dropped for v1 (out of scope,
// see plan) — no external dependency on Espalexa.h in this build.

void sendAllDevices();
void handleBinUpdate(int local);
int disconnectcount = 0;
IPAddress apIP=IPAddress(0,0,0,0);
#define dbgPrintln
#ifndef dbgPrintln
#define DEBUG_WIFI_MULTI(fmt, ...) Serial.printf_P((PGM_P)PSTR(fmt), ##__VA_ARGS__)
#define debugmode
void dbgPrintln(int lvl, String msg)
{
  Serial.println(msg);
}

void dbgPrintln(int lvl, int msg)
{
  Serial.println(msg);
}
#else
#define DEBUG_WIFI_MULTI(...)
#endif

String ID = String(ESP.getChipId());
char outTopic[64]={0};  // allocate enough space
char msgp[1000];        // scratch buffer for getCurrentStatus1() snapshots
long wclienttimedelay=5000;

unsigned long lst_wscon=0;

#include "PolledTimeout.h"
#include <limits.h>
#include <string.h>
#include <vector>

struct WifiAPEntry
{
  char *ssid;
  char *passphrase;
};

typedef std::vector<WifiAPEntry> WifiAPlist;

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 5000
#endif

#ifndef WIFI_SCAN_TIMEOUT_MS
#define WIFI_SCAN_TIMEOUT_MS 5000
#endif

WifiAPlist _APlist;
bool _firstRun = false;
unsigned long lastdecode=0;
char lastdecodestring[301];
void decodeString1(const char *msm, int isServer);
void decodeString(const char *msm, int isServer);
void callback(char *topic, byte *payload, unsigned int length);
void myrun();

uint8_t myrunmode = 0;
long myruntime = 0;
long netsoltime = 0;
uint8_t myruncount = 0;
int8_t scanResult;
int32_t rssi;
uint8_t encType;
uint8_t *bssid;
int32_t channel;
bool hidden;
uint8_t known[20];
uint8_t numNetworks = 0;
uint8_t myrunmodeindex = 0;

bool APlistExists(const char *ssid, const char *passphrase)
{
  if (!ssid || (*ssid == 0x00) || (strlen(ssid) > 32))
  {
    return false;
  }

  for (auto entry : _APlist)
  {
    if (!strcmp(entry.ssid, ssid))
    {
      if (!passphrase)
      {
        if (!strcmp(entry.passphrase, ""))
        {
          return true;
        }
      }
      else
      {
        if (!strcmp(entry.passphrase, passphrase))
        {
          return true;
        }
      }
    }
  }
  return false;
}

void APlistClean(void)
{
  for (auto entry : _APlist)
  {
    if (entry.ssid)
    {
      free(entry.ssid);
    }
    if (entry.passphrase)
    {
      free(entry.passphrase);
    }
  }

  _APlist.clear();
}

void cleanAPlist(void)
{
  APlistClean();
}

bool existsAP(const char *ssid, const char *passphrase)
{
  return APlistExists(ssid, passphrase);
}
bool APlistAdd(const char *ssid, const char *passphrase)
{
  WifiAPEntry newAP;

  if (!ssid || (*ssid == 0x00) || (strlen(ssid) > 32))
  {
    return false;
  }

  if (passphrase && (strlen(passphrase) > 64))
  {
    return false;
  }

  if (APlistExists(ssid, passphrase))
  {
    return true;
  }

  newAP.ssid = strdup(ssid);

  if (!newAP.ssid)
  {
    return false;
  }

  if (passphrase)
  {
    newAP.passphrase = strdup(passphrase);
  }
  else
  {
    newAP.passphrase = strdup("");
  }

  if (!newAP.passphrase)
  {
    free(newAP.ssid);
    return false;
  }

  _APlist.push_back(newAP);
  return true;
}
bool addAP(const char *ssid, const char *passphrase)
{
  return APlistAdd(ssid, passphrase);
}

#include <SocketIOclient.h>

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
WebSocketsClient webSocketClient;
SocketIOclient webSocketIo;
String socketIoPath;
bool socketconnectedio = false;
unsigned long lst_con = 0;
uint16_t uplinkFailCount = 0;
void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length);
void resendCurrentStatus();   // implemented in ESP8266_Newmesh.ino — re-pushes g_callStatus once the uplink reconnects
#include "MYESPNOW.h"
#define EMMDBG_WIFI 1

uint8_t itemId = 0;
int16_t Vdate[20] = {0, 0, 0, 0, 0, 0};

unsigned char h2int(char c)
{
  if (c >= '0' && c <= '9')
  {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f')
  {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F')
  {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

String urldecode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == '+')
    {
      encodedString += ' ';
    }
    else if (c == '%')
    {
      i++;
      if (i >= str.length()) break;
      code0 = str.charAt(i);
      i++;
      if (i >= str.length()) break;
      code1 = str.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    }
    else
    {
      encodedString += c;
    }
  }

  return encodedString;
}

char urlencodes[128]; // bigger buffer

const char *urlencode(const char *str) {
    if (!str || strlen(str) == 0 || strcmp(str, "null") == 0) {
        urlencodes[0] = '\0';
        return urlencodes;
    }

    char *encodedString = urlencodes;
    size_t outLen = 0;
    size_t maxLen = sizeof(urlencodes) - 1;

    for (size_t i = 0; str[i] && outLen < maxLen; i++) {
        char c = str[i];

        if (c == ' ') {
            if (outLen + 1 < maxLen) { *encodedString++ = '+'; outLen++; }
        } else if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') {
            if (outLen + 1 < maxLen) { *encodedString++ = c; outLen++; }
        } else {
            if (outLen + 3 < maxLen) {
                *encodedString++ = '%';
                *encodedString++ = "0123456789ABCDEF"[(c >> 4) & 0xF];
                *encodedString++ = "0123456789ABCDEF"[c & 0xF];
                outLen += 3;
            }
        }
    }

    *encodedString = '\0';
    return urlencodes;
}

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  if (str.length() == 0)
    return "";
  if (str == "null")
    return "";
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    // Percent-encode control characters (e.g. '\n') instead of silently
    // dropping them — a dropped newline in a multi-line settings/debug
    // value used to vanish on the wire with no indication anything was
    // wrong. Bytes >=123 are still passed to the else branch below and
    // percent-encoded normally rather than skipped.
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (c == '.')
    {
      encodedString += '.';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

int connum=-1;
void debugdata(const char *buf)
{
  // Broadcast to every locally-connected WebSocket client (port 81), not
  // just one tracked via connum — that single-client mechanism reset
  // itself to "nobody" the moment its one client sent any message (e.g.
  // the /forms page's "Test" command), silently killing all debug output.
  // Tagged "DBG:" so it's easy to pick out from FORM:/Connected:/status
  // traffic when watching the raw socket.
  String tagged = "DBG:" + String(buf);
  webSocket.broadcastTXT(tagged.c_str());
}

String hourmin(int timehour)
{
  return String((timehour / 60) % 24) + ":" + String((timehour % 60));
}

void sendFormData(int formno);

void copyString(char array[], String s, int sizearray)
{
  strcpy(array, s.substring(0, sizearray - 1).c_str());
}
void writeEpromS(String s, int address, int length)
{
  int i;
  for (i = 0; i < s.length() && i < length; ++i)
  {
    EEPROM.write(address + i, s[i]);
  }
  for (; i < length; i++)
    EEPROM.write(address + i, 0);
}

void getFormData(const char *formdata, int socketnumber);

void getCurrentStatus1(char *buffer, size_t buflen);

bool AmServer()
{
  if (ethercon==1)
  {
    return 1;
  }
  if (WiFi.status() == 3)
  {
    if (WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_"))
      return 0;
    else if (WiFi.SSID().startsWith(MESHNETWORK))
      return 0;
    else
      return 1;
  }
  else
    return 1;
}
bool amServer = 1;
char lastmsgsent[200];
unsigned long lastmsgsentat=0;

bool isConnected()
{
  #ifdef ether86
  if (ethercon==1) return eth.isLinked();
  #else
  if (ethercon==1) return true;
  #endif
  if (WiFi.status()==WL_CONNECTED) return true;
  else return false;
}

int senddata(String cdata,String ip="",String url="")
{
  WiFiClient client;
  int httpPort = 80;
  if (WiFi.status()!=WL_CONNECTED) return 0;
  String host=WiFi.gatewayIP().toString();
  if (!client.connect(host, httpPort)) {
    return 0;
  }

  if (cdata.indexOf("EVENT")<0) return 0;
  cdata.replace(" ","%20");
  String streamId="/send?c="+cdata;
  client.print("GET " + streamId + " HTTP/1.1\r\n" +
               "Host: " + String(host)  + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    ESP.wdtFeed(); yield();
    if (millis() - timeout > 500) {
      client.stop();
      return 0;
    }
  }
  client.stop();
  return 1;
}

void sendToAll(const char *msg, int isServer)
{
  if (outTopic[0]==0) {
    snprintf(outTopic, sizeof(outTopic), "light-out/%s/%s", String(ESP.getChipId(), 16).c_str(), ID.c_str());
  }
  String s;
  if (!strncmp(lastmsgsent,msg,199)){
    if (nowmillis-lastmsgsentat<1000  )
    {
      debugdata(String("SENDTOALL: suppressed duplicate within 1s: '" + String(msg) + "'").c_str());
      return;
    }
  }
  lastmsgsentat=nowmillis;
  strncpy(lastmsgsent,msg,199);

  if (isServer != 2)
    s = ID + "=" + String(msg);
  else
    s = String(msg);

  debugdata(String("SENDTOALL: amServer=" + String(amServer ? 1 : 0) + " msg='" + s + "'").c_str());

  if (amServer)
  {
    if (s.indexOf("EVENT:")<0 && s.indexOf("s")!=0) {
      // Local AP-connected clients (this device's own WebSocket server,
      // port 81) are always reachable regardless of whether STA has
      // joined an upstream network — isConnected() must NOT gate this,
      // only whether ESP-NOW mesh forwarding also makes sense below.
      webSocket.broadcastTXT(s.c_str());
#ifdef ESPNOWACTIVE
  sendMeshMessage(0,s.c_str(),1);
#endif
    }
#ifndef MQTTNOTREQUIRED
    if (isServer != 2)
    {
      if (client.connected())
      {
        if (s.indexOf("update_status") < 0 && s.indexOf("room_connect") < 0)
          client.publish(outTopic, msg);
      }
    }
#endif
  }
  else
  {
    {
      if (webSocketClient.isConnected() && isConnected())
        webSocketClient.sendTXT(s.c_str());
      else
      {
        if (s.indexOf("EVENT:")<0) {
          webSocket.broadcastTXT(s.c_str());
#ifdef ESPNOWACTIVE
  sendMeshMessage(0,s.c_str(),1);
#endif
        }
        if (s.indexOf("EVENT:")>=0 ||  s.indexOf("s")==0)
        {
          s=s+",Web";
          senddata(s.c_str());
        }
      }
    }
  }
}

void sendToServer(String msg, int isServer)
{
  String s;
  s = ID + "=" + msg;
  if (amServer)
  {
  }
  else
  {
if (webSocketClient.isConnected() && isConnected())
    webSocketClient.sendTXT(s.c_str());
        else
    if (s.indexOf("EVENT:")<0)
     webSocket.broadcastTXT(s.c_str());
  }
}

void deviceData(const char*formdata)
{
  if (strncmp(formdata,"DEVICES:",8)==0)
    formdata = formdata+8;
  int formid = -1;
  char *command = strtok((char *)formdata, " ");
  int id10 = 0;
  char *devname;

  if (command != 0)
  {
    id10 = atoi(command);
    command = strtok(NULL, " ");
  }

  if (command != 0)
  {
    devname = command;
    command = strtok(NULL, " ");
  }
  char ext = 'T';
  if (command != 0)
  {
    ext = (*(char *)command);
    command = strtok(NULL, " ");
  }
  // Alexa device registration dropped for v1 — id10/devname/ext parsed above
  // are kept available for a future non-Alexa device-directory feature.
#ifndef MQTTNOTREQUIRED
  if (client.connected())
    Mysubscribe(id10);
#endif
  return;
}
byte parsem = 0, parser = 0;
char parseMessages[10][301];
bool parseisServer[10];
void decodeString2(const char *msm, int isServer)
{
  if (nowmillis-lastdecode<1000 && strncmp(lastdecodestring,msm,300)==0)
  {
    return;
  }
  lastdecode=nowmillis;
  strncpy(lastdecodestring,msm,300);

  if (parsem < 10)
  {
    if (parsem>0 && parsem>parser){
      if (strncmp(parseMessages[parsem-1],msm,300)==0) return;
    }

    strncpy(parseMessages[parsem],msm,300);
    parseisServer[parsem++] = isServer;
  }
}
void parse_out_loop()
{
  while (parsem > parser)
  {
    decodeString1(parseMessages[parser], parseisServer[parser]);
    parseMessages[parser][0] = 0;
    parseisServer[parser] = false;
    parser++;
  }
  if (parsem == parser and parsem > 0)
  {
    parsem = 0;
    parser = 0;
  }
}

#ifndef MQTTNOTREQUIRED
void Mysubscribe(int id, int unsub)
{
  char subscribe[50];
  strlcpy(subscribe, "light-in/", sizeof(subscribe));

  char myID[10];
  itoa(id, myID, 16);
  strlcat(subscribe, (char *)myID, sizeof(subscribe));
  strlcat(subscribe, "/", sizeof(subscribe));
  itoa(id, myID, 10);
  strlcat(subscribe, (char *)myID, sizeof(subscribe));
  if (unsub == 1)
    client.unsubscribe(subscribe);
  else
    client.subscribe(subscribe);
}

void removeAll()
{
  struct deviceStruct *d = espalexa.extradevices;
  while (d != nullptr)
  {
    if (d->id > 0)
    {
      Mysubscribe(d->id, 1);
    }
    d = d->next;
  }
}
void connectAll()
{
  struct deviceStruct *d = espalexa.extradevices;
  while (d != nullptr)
  {
    if (d->id > 0)
    {
      Mysubscribe(d->id, 0);
    }
    d = d->next;
  }
}
#endif

char subscribe[50];
char myID[10];

#ifndef MQTTNOTREQUIRED
char ds[100];
void ForwardPublish(const char*msm)
{
  struct deviceStruct *d = espalexa.extradevices;

  while (d != nullptr)
  {
    if (d->id > 0)
    {
      sprintf(ds,"%d=",d->id);
      if (strncmp(msm,ds,strlen(ds))==0)
      {
        strlcpy(subscribe, "light-out/", sizeof(subscribe));
        itoa(d->id, myID, 16);
        strlcat(subscribe, (char *)myID, sizeof(subscribe));
        strlcat(subscribe, "/", sizeof(subscribe));
        itoa(d->id, myID, 10);
        strlcat(subscribe, (char *)myID, sizeof(subscribe));
        client.publish(subscribe, msm+strlen(ds));
        return;
      }
    }
    d = d->next;
  }
}
#endif
int devicelist = 0;
void decodeString1(const char *msm, int isServer)
{
  debugdata(msm);
  lastdecode=nowmillis;
  if (!msm) return;
  if (strncmp(msm,"DEVICES:",8)==0)
  {
    deviceData(msm);
  }

  if (isServer == 1)
  {
    // A directly-connected local client (this device's own WebSocket
    // server, port 81) is unambiguously talking to exactly this one
    // device — no "<thisDeviceID>=" address prefix required. Commands
    // like "Test"/"GS"/"RESPONSE:..." fall straight through to the
    // dispatch block below as-is.
  }
  else if (isServer == 0 || isServer == 2)
  {
    // Relayed/mesh-origin traffic (this device acting as an uplink client,
    // or a message forwarded over ESP-NOW) DOES need the address prefix
    // stripped, since a shared medium can carry messages meant for other
    // devices too.
    debugdata(String("Received by Client from above ").c_str());
    debugdata(msm);
    if (strstr(msm,"EVENT:")) return;
    if (strstr(msm,"R1Gnull")) return;
    if (strncmp(msm,"NDEVICELIST",10)==0)
    {
      devicelist=0;
      String msg = "DEVICES:" + ID + " " + myConfig.myDeviceName + " " + String(myConfig.myDeviceIdentification);
      sendToAll(msg.c_str(), 2);
      decodeString(msm, 0);
      return;
    }

    char buf[1024];
    sprintf_P(buf, PSTR("%s="),ID.c_str());

    if (strncmp(msm,buf,strlen(buf))==0)
    {
      msm = msm+strlen(buf);
    }
    else
    {
#ifdef MASTERCONTROLLER
      if (strncmp(msm,"s",1)==0){
        decodeString(msm,0);
      }
#endif
#ifdef NURSECALLNEW
      if (strncmp(msm,"j",1)==0){
        decodeString(msm,0);
      }
      if (strncmp(msm,"os",2)==0 && *(msm+strlen(msm)-1)=='9'){
        decodeString(msm,0);
      }
      if (strstr(msm,"=s")){
        decodeString(msm,0);
      }
#endif
      return;
    }
  }

  if (!strncmp(msm,"SOCKETCLIENTCONNECTED",10))
  {
    devicelist=1;
  }
  else
  if (!strncmp(msm,"GS",2))
  {
    getCurrentStatus1(msgp, sizeof(msgp));
    sendMeshMessage(0,String(msgp),1);
  }
  else if (!strncmp(msm,"Reboot",7))
  {
    ESPrestart();
  }
  else if (!strncmp(msm,"Reset",6))
  {
    ESPrestart();
  }
  else if (!strncmp(msm,"Test",5))
  {
    sendFormData(0);
  }
  else if (!strncmp(msm,"RESPONSE:",9))
  {
    getFormData(msm, 2);
  }
  else if (!strncmp(msm,"UPDATE",7))
  {
    ESP.wdtFeed();
    handleBinUpdate(1);
  }
  else
    decodeString(msm, 0);
}
int wscdisconnect = 0;
void webSocketEventClient(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    debugdata(String("Uplink WebSocket DISCONNECTED (reconnect count now " + String(wscdisconnect + 1) + ")").c_str());
    lst_wscon=0;
    if (isConnected())
      wscdisconnect++;
    if (wscdisconnect > 20 && !amServer)
    {
        wscdisconnect=0;
    }
    break;
  case WStype_CONNECTED:
  {
    debugdata("Uplink WebSocket CONNECTED");
    lst_wscon= millis();
    wscdisconnect = 0;
    disconnectcount = 0;
    if (webSocketClient.isConnected() && isConnected())
    webSocketClient.sendTXT("ESP");
    String msg = "DEVICES:" + ID + " " + String(myConfig.myDeviceName) + " " + String(myConfig.myDeviceIdentification);
    if (webSocketClient.isConnected() && isConnected())
    webSocketClient.sendTXT(msg);
    sendAllDevices();
  }
  break;
  case WStype_TEXT:
    {
    if (strstr((const char *)payload,"INFO:")) return;
    if (strstr((const char *)payload,"Info:")) return;
    if (strstr((const char *)payload,"o")==(const char *)payload) {
      if (strstr((const char *)payload,",9")==NULL){
        return;
      }
    }

    if (strstr((const char *)payload,"IGNORE:")) return;
    if (strstr((const char *)payload,"R1G")==(const char *)payload) return;
    debugdata(String("Received by webSocketClient").c_str());
    debugdata((const char *)payload);

    if (amServer)
    {
#ifdef NURSECALLNEW
    sendMeshMessage(0,(const char *)payload,1,2);
#endif
    }
    else
    {
    }
    if (amServer==0){
      decodeString2((const char *)payload, 0);
    }
    else{
      decodeString2((const char *)payload, 1);
    }
    }
    break;
  case WStype_BIN:
{
  if (length != sizeof(MeshPacket)) return;
  int nextHead = (rxHead + 1) % RX_QUEUE_SIZE;
  if (nextHead == rxTail) {
    return;
  }
  memcpy(&rxQueue[rxHead], payload, length);
  rxHead = nextHead;
  }
    break;
  case WStype_PING:
    break;
  case WStype_PONG:
    break;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    if (connum==num) connum=-1;
    if(num<5)
    ESPN[num]=0;
    break;
  case WStype_CONNECTED:
  {
    if (connum==-1) connum=num;
    if(num<5)
    ESPN[num] = 1;
    IPAddress ip = webSocket.remoteIP(num);
    webSocket.sendTXT(num, String("Connected:" + ID + ":1").c_str());
  }
  break;
  case WStype_PING:
  case WStype_PONG:
  break;
  case WStype_TEXT:
  {
    if (connum==num) connum=-1;
    if (strstr((const char *)payload,"IGNORE:")) return;
    if(num<5)
    if ((char)payload[0] == 'E' && (char)payload[1] == 'S' && (char)payload[2] == 'P' && length == 3)
    {
      ESPN[num] = 2;
      devicelist=1;
      return;
    }

    if (amServer)
    {
#ifndef MQTTNOTREQUIRED
        if (amServer==1){
          if (client.connected())
          {
            {
              ForwardPublish((const char *)payload);
            }
          }
        }
#endif
#ifdef ESPNOWACTIVE
    sendMeshMessage(0,(const char *)payload,1,2);
#endif
    }
    else
    {
#ifdef ESPNOWACTIVE
      sendMeshMessage(0,(const char *)payload,1);
#endif
    }
    decodeString2((const char *)payload, 1);
  }
    break;
  case WStype_BIN:
  if (length != sizeof(MeshPacket)) return;
  int nextHead = (rxHead + 1) % RX_QUEUE_SIZE;
  if (nextHead == rxTail) {
    return;
  }
  memcpy(&rxQueue[rxHead], payload, length);
  rxHead = nextHead;
    break;
  }
}

// ==========================================
// AP identity — always-on, two live states
// ==========================================
// The device runs AP+STA concurrently at all times, regardless of which
// route (WiFi mesh / RS485 / Ethernet) is selected, so on-site staff can
// always reach the settings page — the AP never turns off. Its IDENTITY
// does still reflect live connection state, matching the pre-rewrite
// behavior: standalone (no upstream network joined) broadcasts
// "NETE<asccode><id>" on 192.168.6.x; once bridged to a real upstream
// WiFi network it switches to "MESH<asccode><id>" on 192.168.(100+id).x
// so other mesh nodes can find it. Both asccode/id are read live, so a
// settings change takes effect the next time this re-evaluates.
bool apInitialized = false;
bool lastApConnectedState = false;   // forces the first setup_AP() call through
void setup_AP(bool forceRestart)
{
  if(manulalAP==true) return ;

  bool connected = isConnected();

  if (apInitialized && !forceRestart) return;
  if (apInitialized && connected == lastApConnectedState) return;   // nothing actually changed
  apInitialized = true;
  lastApConnectedState = connected;

  uint8_t octet3 = connected ? (100 + (myConfig.machineid & 0xFF)) : 6;
  if (octet3 > 250 || octet3 == 0) octet3 = 6;

  apIP = IPAddress(192, 168, octet3, 1);
  IPAddress apGateway(apIP);
  IPAddress apSubmask(255, 255, 255, 0);
  WiFi.softAPdisconnect(true);
  WiFi.softAPConfig(apIP, apGateway, apSubmask);

  String apSsid = connected ? (MESHNETWORK + String(myConfig.asccode) + String(myConfig.machineid))
                             : ("NETE" + String(myConfig.asccode) + String(myConfig.machineid));
  const char *apPassword = connected ? "MESHPASSWORD" : "Netsol@123";
  WiFi.softAP(apSsid.c_str(), apPassword, myConfig.wifiChannel ? myConfig.wifiChannel : 1, 0, 4);
  dbgPrintln(1, "Initialized AP as IP '" + apIP.toString() + "' SSID " + apSsid);
}

// ==========================================
// Uplink (device -> central server) connect/reconnect
// ==========================================
// Single place that (re)establishes the uplink, whether that's a mesh-peer
// gateway (plain WebSocket to WiFi.gatewayIP():81) or the configured
// external server via myConfig.myServer/myPort — as WebSocket or Socket.IO
// depending on myConfig.socketio. Called once when STA first gets an IP,
// and again periodically by uplinkHealthCheck() below whenever the link is
// found to be down, matching the legacy firmware's reconnectServer().
void connectUplink()
{
  if (WiFi.SSID().startsWith(MESHNETWORK) || WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_"))
  {
    debugdata(String("Uplink: connecting to mesh gateway " + WiFi.gatewayIP().toString() + ":81").c_str());
    if (webSocketClient.isConnected()) webSocketClient.disconnect();
    webSocketClient.begin(WiFi.gatewayIP(), 81, "/");
    webSocketClient.onEvent(webSocketEventClient);
    webSocketClient.setReconnectInterval(2000);
    webSocketClient.enableHeartbeat(3000, 3000, 2);
    return;
  }

  if (strlen(myConfig.myServer) == 0)
  {
    debugdata("Uplink: myServer is empty - nothing to connect to");
    return;
  }

  if (myConfig.socketio)
  {
    webSocketIo.close();
    socketIoPath = "/socket.io/?asccode=" + String(myConfig.asccode) + "&d=0&type=1&EIO=3";
    debugdata(String("Uplink: connecting SocketIO to " + String(myConfig.myServer) + ":" + String(myConfig.myPort) + socketIoPath).c_str());
    webSocketIo.begin(myConfig.myServer, myConfig.myPort, socketIoPath, "");
    webSocketIo.onEvent(socketIOEvent);
  }
  else
  {
    debugdata(String("Uplink: connecting WebSocket to " + String(myConfig.myServer) + ":" + String(myConfig.myPort)).c_str());
    if (webSocketClient.isConnected()) webSocketClient.disconnect();
    webSocketClient.begin(myConfig.myServer, myConfig.myPort, "/", "");
    webSocketClient.onEvent(webSocketEventClient);
    webSocketClient.setReconnectInterval(2000);
    webSocketClient.enableHeartbeat(3000, 3000, 2);
  }
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
    case sIOtype_DISCONNECT:
      if (!socketconnectedio) { lst_con = millis(); return; }
      socketconnectedio = false;
      lst_con = millis();
      debugdata("Uplink SocketIO DISCONNECTED");
      break;
    case sIOtype_CONNECT:
      socketconnectedio = true;
      lst_con = millis();
      debugdata("Uplink SocketIO CONNECTED");
      webSocketIo.send(sIOtype_CONNECT, "/");   // join default namespace - required, no auto-join in Socket.IO v3+
      break;
    default:
      break;
  }
}

// Periodic (called from myrun()) — verifies the uplink is actually up,
// retries via connectUplink() every 3rd failed check, re-pushes current
// status once a reconnect succeeds, and restarts the device if the link
// has been down for a very long time — matching the legacy firmware's
// dcount/webSocketNotConnected/reconnectServer() health-check loop.
void uplinkHealthCheck()
{
  if (ethercon == 1) return;                 // Ethernet route doesn't use this WiFi uplink
  if (WiFi.status() != WL_CONNECTED) return;  // STA not even joined yet - nothing to check

  bool isMeshPeer = WiFi.SSID().startsWith(MESHNETWORK) || WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_");
  if (!isMeshPeer && strlen(myConfig.myServer) == 0) return;   // nothing configured to connect to

  bool up = (myConfig.socketio && !isMeshPeer) ? webSocketIo.isConnected() : webSocketClient.isConnected();

  if (up)
  {
    if (uplinkFailCount > 0)
    {
      debugdata("Uplink: reconnected - resending current status");
      resendCurrentStatus();
    }
    uplinkFailCount = 0;
    return;
  }

  uplinkFailCount++;
  debugdata(String("Uplink: DOWN (fail count " + String(uplinkFailCount) + ")").c_str());
  if (uplinkFailCount % 3 == 0)
  {
    connectUplink();
  }
  if (uplinkFailCount > 12 && millis() > 120000)
  {
    debugdata("Uplink: down too long - restarting device");
    delay(100);
    ESP.restart();
  }
}

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler, SoftAPModeStationDisconnected, SoftAPModeStationConnected;

byte ssidconnected=0;
void connectWiFiEvents()
{
  SoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected &event)
                                                                 {
                                                                 });

  SoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected &event)
                                                                       {
                                                                       });

  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event)
                                              {
                                                IPAddress ipipad;
                                                ipipad = WiFi.localIP();
                                                debugdata(String("STA got IP " + ipipad.toString() + " on SSID '" + WiFi.SSID() + "'").c_str());
                                                if (ipipad[0] <= 0)
                                                {
                                                  debugdata("STA IP invalid (0.x.x.x) - disconnecting and retrying");
                                                  WiFi.disconnect();
                                                  return;
                                                }
                                                if (WiFi.SSID() == myConfig.mySSID)
                                                {
                                                  ssidconnected=0;
                                                  String ipad = myConfig.myIP;
                                                  String netm = myConfig.myNetmask;
                                                  String gatw = myConfig.myGateway;
                                                  IPAddress ipipad, ipnetm, ipgatw,ipdns;
                                                  ipdns.fromString("8.8.8.8");
                                                  if (ipipad.fromString(ipad.c_str()) == 1 && ipnetm.fromString(netm.c_str()) == 1 && ipgatw.fromString(gatw.c_str()) == 1)
                                                  {
                                                    if (ipipad[0] > 0)
                                                      WiFi.config(ipipad, ipgatw, ipnetm, ipgatw,ipdns);
                                                  }
                                                }
                                                amServer = AmServer();
                                                setup_AP(true);   // re-evaluate AP subnet now that STA is bridged (SSID unchanged)
                                                if (WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_") )
                                                  netsoltime = nowmillis;
                                                else
                                                  netsoltime = 0;
#ifndef MQTTNOTREQUIRED
                                                if (amServer==1 && !(WiFi.SSID().startsWith(MESHNETWORK) || WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_"))){
                                                  client.setServer("www.ask4token.com", 1883);
                                                  client.setCallback(callback);
                                                }
#endif
                                                uplinkFailCount = 0;
                                                connectUplink();
                                              });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
                                                            {
                                                              disconnectcount++;
                                                              ssidconnected++;
                                                              if (ssidconnected<5)
                                                              {
                                                                WiFi.reconnect();
                                                                myruntime = nowmillis+10000;
                                                                return;
                                                              }
                                                              ssidconnected = 0;
                                                              if (disconnectcount > 30)
                                                              {
                                                                disconnectcount = 0;
                                                              }
                                                              WiFi.disconnect(true);
                                                              amServer = AmServer();
                                                              webSocketClient.setReconnectInterval(300000);
                                                              if (webSocketClient.isConnected()) webSocketClient.disconnect();
                                                              // AP stays up regardless of STA connection state — only its
                                                              // subnet falls back to standalone (192.168.6.x); SSID unchanged.
                                                              setup_AP(true);
                                                              myruntime = nowmillis+10000;
                                                            });
}

char messg[1000];
void callback(char *topic, byte *payload, unsigned int length)
{
  char *data = (char *)topic;
  if (strstr(data, "/"))
    data = strstr(data, "/") + 1;
  if (strstr(data, "/"))
    data = strstr(data, "/") + 1;
  strlcpy(messg, data, sizeof(messg));
  strlcat(messg, "=", sizeof(messg));
  size_t messglen = strlen(messg);
  memcpy(messg + messglen, payload, length);
  messg[messglen + length] = 0;

  sendToAll(messg,2);
  decodeString2(messg, 2);
}
#ifndef MQTTNOTREQUIRED
boolean mqttconnected = 0;
uint8_t mqttserver = 0;
int value = 0;
boolean reconnect()
{
  String clientId = "EClient-";
  clientId += ID;
  const char *username = "mqtt_user_name";
  const char *password = "mqtt_password";
  mqttserver++;
  if (mqttserver > 3)
    mqttserver = 0;
  client.setServer(mqtt_server[mqttserver], 1883);
  client.setCallback(callback);
  client.setBufferSize(1024);
  if (!isConnected()) return false;

  if (client.connect(clientId.c_str(), username, password))
  {
    mqttconnected = 1;
    Mysubscribe(atoi(ID.c_str()));
    connectAll();
    return true;
  }
  return client.connected();
}

long lastReconnectAttempt = 0;
void mqttloop()
{
  if (amServer && isConnected())
  {
    if (!client.connected())
    {
      mqttconnected = 0;
      long now = nowmillis;
      if (now - lastReconnectAttempt > wclienttimedelay)
      {
        lastReconnectAttempt = now;
        espClient.setTimeout(1000);
        if (reconnect())
        {
          wclienttimedelay=1000;
          lastReconnectAttempt = 0;
        }
        if (wclienttimedelay<180000)
          wclienttimedelay=wclienttimedelay+5000;
      }
    }
    else
    {
      client.loop();
      unsigned long now = nowmillis;
      if (now - lastMsg > 2000)
      {
        lastMsg = now;
        ++value;
      }
    }
  }
}
#endif
unsigned long last_10sec = 0, dcount = 0;
unsigned int counter = 0;
uint8_t firsttime = 0;
unsigned long wifimultirun = 0;

void tdinputn(const char *spanstring,
              const char *nameString,
              const char *required,
              const char *nameid,
              int maxlength,
              const char *valuestring,
              int colspan,
              char *outBuf,
              size_t outLen)
{
    snprintf(outBuf, outLen,
        "<td colspan=\"%d\">"
        "<label for=\"%s\">"
        "<span %s>%s"
        "<span class=\"required\">%s</span></span>"
        "<input type=\"text\" class=\"input-field\" id=\"%s\" "
        "name=\"%s\" value=\"%s\" maxlength=\"%d\" />"
        "</label></td>",
        colspan, nameid, spanstring, nameString,
        required, nameid, nameid, valuestring, maxlength);
}

// ==========================================
// /forms — minimal browser client for the WebSocket settings-form protocol
// ==========================================
// This is the ONLY UI for viewing/changing settings now that the old
// HTML-form-generator page is gone. It speaks the exact same
// $NEXT$/$DONE$/$T|/$D|/$R| + "RESPONSE:formid|argk|value ..." protocol
// DeviceProtocol.cpp's sendFormData()/getFormData() already implement — no
// separate companion app is required, any browser works.
const char FORMS_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nurse Call Settings</title>
<style>
body{font-family:sans-serif;margin:0;padding:16px;background:#f4f4f4;color:#222}
h2{margin-top:0}
.field{margin-bottom:12px}
label{display:block;font-weight:bold;margin-bottom:4px;font-size:14px}
input,select,textarea{width:100%;padding:8px;box-sizing:border-box;font-size:16px}
button{padding:10px 16px;font-size:15px;margin:4px 6px 4px 0;cursor:pointer}
#menu button{display:block;width:100%;text-align:left;margin-bottom:6px}
#status{color:#666;font-size:13px;margin-bottom:14px}
</style></head>
<body>
<h2>Nurse Call Settings</h2>
<div id="status">Connecting...</div>
<div id="app"></div>
<script>
var ws, formId = -1;
function setStatus(s) { document.getElementById("status").textContent = s; }
function send(s) { if (ws && ws.readyState === 1) ws.send(s); }
function connect() {
  ws = new WebSocket("ws://" + location.hostname + ":81/");
  ws.onopen = function () { setStatus("Connected, loading menu..."); };
  ws.onclose = function () { setStatus("Disconnected - retrying..."); setTimeout(connect, 2000); };
  ws.onerror = function () { setStatus("Connection error"); };
  ws.onmessage = function (e) { handleMessage(e.data); };
}
function handleMessage(raw) {
  var msg = raw;
  var eq = msg.indexOf("=");
  if (eq > 0 && eq < 20 && msg.indexOf("FORM:") !== 0 && msg.indexOf("Connected") !== 0) {
    msg = msg.substring(eq + 1);
  }
  if (msg.indexOf("Connected:") === 0) { send("Test"); return; }
  if (msg.indexOf("FORM:") === 0) { renderForm(msg); return; }
}
function renderForm(msg) {
  var body = msg.substring(5);
  var idEnd = body.indexOf("$");
  formId = parseInt(body.substring(0, idEnd), 10);
  var tokens = body.substring(idEnd).split("$").filter(function (t) { return t.length > 0; });
  var title = "Settings", fields = [], isMenu = false, menuOptions = [];
  tokens.forEach(function (tok) {
    if (tok.indexOf("NEXT$") === 0 || tok.indexOf("DONE$") === 0) {
      title = tok.substring(5);
    } else if (tok.indexOf("T|") === 0) {
      var p = tok.substring(2).split("|");
      fields.push({ type: "T", label: p[0], value: decodeURIComponent(p[2] || "") });
    } else if (tok.indexOf("D|") === 0) {
      var p = tok.substring(2).split("|");
      var opts = p[1].split("#").map(function (o) { var kv = o.split(":"); return { label: kv[0], value: kv[1] }; });
      fields.push({ type: "D", label: p[0], options: opts, value: p[2] });
    } else if (tok.indexOf("R|") === 0) {
      var p = tok.substring(2).split("|");
      isMenu = true;
      menuOptions = p[1].split("#").map(function (o) { var kv = o.split(":"); return { label: kv[0], value: kv[1] }; });
    }
  });
  var app = document.getElementById("app");
  app.innerHTML = "";
  var h = document.createElement("h3"); h.textContent = title; app.appendChild(h);
  setStatus("Form " + formId + " loaded");
  if (isMenu) {
    var div = document.createElement("div"); div.id = "menu";
    menuOptions.forEach(function (o) {
      var b = document.createElement("button");
      b.textContent = o.label;
      b.onclick = function () { send("RESPONSE:0|0 0|" + o.value); };
      div.appendChild(b);
    });
    app.appendChild(div);
    return;
  }
  var form = document.createElement("div");
  fields.forEach(function (f, i) {
    var wrap = document.createElement("div"); wrap.className = "field";
    var lab = document.createElement("label"); lab.textContent = f.label; wrap.appendChild(lab);
    var input;
    if (f.type === "D") {
      input = document.createElement("select");
      f.options.forEach(function (o) {
        var opt = document.createElement("option");
        opt.value = o.value; opt.textContent = o.label;
        if (o.value === f.value) opt.selected = true;
        input.appendChild(opt);
      });
    } else if (f.label === "Debug") {
      input = document.createElement("textarea");
      input.readOnly = true;
      input.rows = 10;
      input.style.width = "100%";
      input.style.boxSizing = "border-box";
      input.style.fontFamily = "monospace";
      input.style.fontSize = "13px";
      input.style.whiteSpace = "pre-wrap";
      input.value = f.value;
    } else {
      input = document.createElement("input");
      input.type = "text";
      input.value = f.value;
    }
    input.dataset.idx = i;
    wrap.appendChild(input);
    form.appendChild(wrap);
  });
  app.appendChild(form);
  var saveBtn = document.createElement("button");
  saveBtn.textContent = "Save";
  saveBtn.onclick = function () {
    var inputs = form.querySelectorAll("input,select");
    var parts = ["RESPONSE:" + formId + "|" + formId];
    inputs.forEach(function (inp) { parts.push(inp.dataset.idx + "|" + encodeURIComponent(inp.value)); });
    send(parts.join(" "));
    setStatus("Saved, reloading...");
  };
  app.appendChild(saveBtn);
  var backBtn = document.createElement("button");
  backBtn.textContent = "Back to Menu";
  backBtn.onclick = function () { send("Test"); };
  app.appendChild(backBtn);
}
connect();
</script>
</body></html>
)rawliteral";

uint32_t ith=0;
uint32_t getInternalBootCode()
{
    uint32_t result;
    ESP.rtcUserMemoryRead (0, (uint32_t*) &result, sizeof(result));
    ith=result;
    return result;
}

void setInternalBootCode(uint32_t value)
{
    uint32_t storeValue = value;
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &storeValue, sizeof(storeValue));
    getInternalBootCode();
}

void startconnection()
{
  // Config is already loaded (with structVersion-guarded defaults applied)
  // by configLoad() in NurseCallConfig.cpp, called once from setup() before
  // this — do not reload/overwrite myConfig here.
  getInternalBootCode();
  if (myConfig.machineid<0) myConfig.machineid=0;
  WiFi.persistent(false);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.setOutputPower(20.5);
  setup_AP();
  connectWiFiEvents();

  addAP("test_ms", "netsol@123MS");

  if (String(myConfig.mySSID).length() > 3 && String(myConfig.myPass).length() > 3 && ethercon==0)
    addAP(myConfig.mySSID, myConfig.myPass);

  if (myConfig.machineid>0)
    addAP(String(MESHNETWORK+String(myConfig.asccode)+String(myConfig.machineid-1)).c_str(), "MESHPASSWORD");
  if (myConfig.machineid>1)
    addAP(String(MESHNETWORK+String(myConfig.asccode)+String(myConfig.machineid-2)).c_str(), "MESHPASSWORD");

  addAP("homeauto", "HA@123home");
  addAP("test_ms10", "netsol@123MS");

  myrun();

  webSocket.begin();
  webSocket.enableHeartbeat(3000, 3000, 2);
  webSocket.onEvent(webSocketEvent);

  server.on("/forms", []()
            {
              server.send_P(200, "text/html", FORMS_PAGE);
            });
  server.on("/reboot", []()
            {
              server.send(200, "text/html", "OK");
              ESPrestart();
            });
  server.on("/send", []()
            {
              sendToAll(server.arg("c").c_str(),2);
              decodeString2(server.arg("c").c_str(),1);
              server.send(200, "text/html", "OK");
            });

  server.on("/settingn", []()
            {
            copyString(myConfig.mySSID,server.arg("ssid"),sizeof(myConfig.mySSID));
            copyString(myConfig.myPass,server.arg("pass"),sizeof(myConfig.myPass));
            copyString(myConfig.myDeviceName,server.arg("ascs"),sizeof(myConfig.myDeviceName));
            myConfig.machineid=atoi(server.arg("ports").c_str());
            copyString(myConfig.myIP,server.arg("ips"), sizeof(myConfig.myIP));
            copyString(myConfig.myGateway,server.arg("gates"), sizeof(myConfig.myGateway));
            copyString(myConfig.myNetmask,server.arg("netmasks"), sizeof(myConfig.myNetmask));

   EEPROM.put(100, myConfig);
    EEPROM.commit();
    ESP.wdtFeed();

    String content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}<script>document.location='/reboot';</script>";
  server.send(200, "text/html", content);
            });
  server.on("/id", []() {
    String s = "MySetID:"+String(ID)+"\n";
    server.send(200, "text/plain", s);
  });

  server.on("/", []()
            {
              getCurrentStatus1(msgp, sizeof(msgp));
              String msgs=String(msgp);
              msgs.replace(",", "<br>");
              server.send(200, "text/html", msgs + F("<br><a href='/forms'>Settings</a><br><a href='/reboot'>Reboot</a>"));
            });

  server.on("/refreshlist", HTTP_GET, []()
            {
              devicelist=1;
              server.send(200, "text/plain", "Refresh Ok");
            });

  server.begin();
  if (myConfig.myDeviceIdentification > 127)
    myConfig.myDeviceIdentification = 'T';
}

long lastgettime = 0;

// Device-directory (Alexa extradevices list) dropped for v1 along with
// Espalexa — both are now no-ops kept only so existing call sites compile
// unchanged; DeviceProtocol.cpp owns device status going forward.
void sendAllDevices()
{
}
void removeExtra()
{
}

void myrun()
{
  unsigned long t = millis();
  nowmillis=millis();
  wl_status_t status;
  parse_out_loop();
  webSocket.loop();

  if (isConnected()) {
    webSocketClient.loop();
    webSocketIo.loop();
  }

#ifndef MQTTNOTREQUIRED
  if (amServer==1)
    mqttloop();
#endif

  server.handleClient();
  status = WiFi.status();
  if (ethercon==1) status=WL_CONNECTED;
  if ((firsttime < 120) && (t>30000) && (status!=WL_CONNECTED))
  {
      firsttime++;
      if (firsttime == 120)
      {
        if (myConfig.type[0]==1){
          if (myConfig.machineid>0)
            addAP(String(MESHNETWORK+String(myConfig.asccode)+String(myConfig.machineid-1)).c_str(), "MESHPASSWORD");
          if (myConfig.machineid>1)
           addAP(String(MESHNETWORK+String(myConfig.asccode)+String(myConfig.machineid-2)).c_str(), "MESHPASSWORD");
        }
      }
  }

  if ((t - last_10sec) > 5000)
  {
    if (devicelist==1)
    {
      devicelist = 0;
    }
    if (ESP.getFreeHeap()<5100)
    {
      setup_AP(true);
    }

    uplinkHealthCheck();

    counter++;
    last_10sec = t;
  }
  if (t < myruntime)
    return;
  if (t - myruntime < 1000)
    return;

  myruntime = t;
  removeExtra();

  if (ethercon==1) return;
  if (_firstRun)
  {
    _firstRun = false;
    if (strlen(WiFi.SSID().c_str()))
    {
      WiFi.begin();
    }
  }

  if (status == WL_CONNECTED)
  {
    if (firsttime<120) firsttime=0;
    if (WiFi.SSID().startsWith("NETSOL") || WiFi.SSID().startsWith("NOW_") )
    {
      if (t - netsoltime > 300000 && netsoltime > 0)
      {
        WiFi.disconnect();
        myrunmode = 0;
        return;
      }
    }
    if (WiFi.status() == 3 && WiFi.localIP().toString() == "(IP unset)")
      WiFi.disconnect(true);
    myruncount = 0;
    return;
  }

  if (myrunmode == 0)
  {
    if (myConfig.type[0]==2 && myConfig.mesh_en) return;
    WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.disconnect();
    WiFi.scanNetworks(true);
    myrunmode = 1;
    myruncount++;
    return;
  }

  if (myrunmode == 1)
  {
    scanResult = WiFi.scanComplete();
    if (scanResult < 0)
      return;
    myrunmode = 2;
  }

  if (myrunmode == 2)
  {
    numNetworks = 0;
    for (int8_t i = 0; i < scanResult && numNetworks < 20; i++)
    {
      String ssid;
      WiFi.getNetworkInfo(i, ssid, encType, rssi, bssid, channel, hidden);
      for (auto entry : _APlist)
      {
        if (ssid == entry.ssid)
        {
          known[numNetworks++] = i;
          break;   // one match per scanned network is enough; also bounds numNetworks < 20 via the outer loop guard
        }
      }
    }
    if (numNetworks == 0)
    {
      myrunmode = 3;
      for (uint8_t j = 0; j < _APlist.size(); j++)
      {
        auto &entry = _APlist[j];
        WiFi.begin(entry.ssid, entry.passphrase);
        break;
      }
      myrunmodeindex=1;
      myruntime = myruntime + 30000;
      ssidconnected=0;
      return;
    }

    myrunmode = 3;
    for (int i = 0; i < numNetworks; i++)
    {
      for (int j = i + 1; j < numNetworks; j++)
      {
        if (WiFi.RSSI(known[j]) > WiFi.RSSI(known[i]))
        {
          int8_t tmp;
          tmp = known[i];
          known[i] = known[j];
          known[j] = tmp;
        }
      }
    }
    myrunmodeindex = 0;
  }
  if (myrunmode == 3)
  {
    if (myrunmodeindex == numNetworks)
    {
      myruncount++;
      if (myruncount > 15)
      {
        myruncount = 0;
      }
      myrunmode = 0;
      myruntime = myruntime + 1000;
      return;
    }
    String ssid;
    WiFi.getNetworkInfo(known[myrunmodeindex], ssid, encType, rssi, bssid, channel, hidden);

    for (uint8_t j = 0; j < _APlist.size(); j++)
    {
      auto &entry = _APlist[j];
      if (ssid == entry.ssid)
      {
        if (ssid == "NETSOL" && (WiFi.softAPgetStationNum() > 0 || myruncount < 10))
        {
        }
        else
          WiFi.begin(ssid, entry.passphrase, channel, bssid);
        ssidconnected=0;
        myrunmodeindex++;
        myruntime = myruntime + 30000;
        return;
      }
    }

    myrunmode = 0;
    myruntime = myruntime + 1000;
    return;
  }

  return;
}

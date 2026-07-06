/**
 * ESP8266 ESP-NOW Mesh with Simultaneous SoftAP Config
 *
 * HARDWARE:
 * - ESP8266 NodeMCU or Wemos D1 Mini
 * - LED on Builtin LED pin (usually D4/GPIO2)
 * - Button/Jumper on CONFIG_PIN (D3/GPIO0) - Hold to Factory Reset
 *
 * This file is the vendored ESP-NOW mesh packet layer. It is intentionally
 * left structurally unchanged from the working foundation project
 * (NurseCall_ser4_ESPNOW) — MeshPacket's layout and the 212-byte
 * esp_now_send() size are load-bearing and must not change here.
 *
 * The only addition versus the foundation is meshLayerActive(), used by
 * TransportMeshWifi so the rest of the firmware never reaches into the
 * esp8266_now_active global directly.
 */

#include <espnow.h>
void decodeString2(const char *msm, int isServer);
// Global flag for ESP8266 (Since it lacks a check API)
bool esp8266_now_active = false;
bool new_data = false;
uint8_t espnowaval = 0;   // set to 200 on ack/keepalive traffic; reserved for future mesh-health reporting (not yet read anywhere)

// ==========================================
// Configuration & Constants
// ==========================================
#define CONFIG_PIN 0        // GPIO0 (Flash Button)
#define STATUS_LED 2        // Built-in LED
#define EEPROM_SIZE 512
#define MAX_HOPS 5          // Default TTL
#define BROADCAST_ADDR 0xFFFF
#define RX_QUEUE_SIZE 6   // Number of packets to buffer

// MESH LIMITS
#define MAX_NODES 151      // Supports Node IDs 0-1000
#define REBOOT_THRESHOLD 10 // Gap to detect if a node rebooted (e.g. Seq went from 1000 -> 1)

// Data Packet Structure (Packed to ensure byte-alignment)
struct __attribute__((packed)) MeshPacket {
  uint16_t magic;       // Mesh ID
  uint32_t msgId;       // Unique Message ID
  uint16_t sourceId;
  uint16_t destId;
  uint8_t  ttl;
  uint8_t  type;        // 0=Data, 1=Ping, 2=Ack
  char     payload[200];
};

uint8_t retransmit = 1;

// Define the structures
struct DeviceData {
  uint8_t id;
  uint8_t status;
  int deviceid;
} nowdeviceData;

// Global Variables
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- Sequence Management ---
uint16_t mySequenceIdx = 1; // My outgoing sequence counter
uint16_t nodeSequenceTracker[MAX_NODES]; // The "Log" of last seen sequence per node

uint16_t test_meshID = 0;

// RX Queue (Ring Buffer)
MeshPacket rxQueue[RX_QUEUE_SIZE];
volatile int rxHead = 0;
volatile int rxTail = 0;

// ==========================================
// Helper Functions
// ==========================================

void loadSettings() {
#ifndef rxserver
  if (myConfig.nodeId == 1) myConfig.nodeId = ((uint16_t)ESP.getChipId()) % 200;
  if (myConfig.nodeId == 1) myConfig.nodeId = (millis() % 200);
  if (myConfig.nodeId == 1) myConfig.nodeId = random(2, 200);
#endif
}

void saveSettings() {
}

void factoryReset() {
  myConfig.Fix = 0x00;
  saveSettings();
}

bool meshLayerActive() { return esp8266_now_active; }

// ==========================================
// Mesh Sending Logic
// ==========================================

void debugdata(const char *buf);

void webSocketbroadcastTXT(uint8_t *ps, uint8_t len)
{
  char *p = (char *)ps;
  if (strlen(p) < 2) return;
  for (int j = 0; j < 5; j++) {
    if (ESPN[j] == 1) {
      webSocket.sendTXT(j, p);
    }
  }
}

void sendMeshBinary(uint16_t destId, void* data, uint8_t len, uint8_t type, uint8_t ttl = MAX_HOPS) {
  if (!esp8266_now_active) {
    if (len > 10) {
      char *p = (char *)data;
      webSocket.broadcastTXT(p);
    }
    return; // ESP-NOW not initialized, skip sending
  }

  if (len > 200) {
    return;
  }

  MeshPacket p;
  p.magic = myConfig.meshId;
  p.sourceId = myConfig.nodeId;
  p.destId = destId;
  p.ttl = ttl;
  p.type = type;

  // --- Incremental ID: skip 0 to avoid init confusion ---
  mySequenceIdx++;
  if (mySequenceIdx <= 0) mySequenceIdx = 1;
  p.msgId = mySequenceIdx;

  memcpy(p.payload, data, len);

  webSocketbroadcastTXT((uint8_t *)data, 0);

  if (millis() < 2000) return;
  esp_now_send(broadcastMac, (unsigned char *)&p, 212);
  if (test_meshID != 0 && test_meshID != 999) {
    p.magic = test_meshID;
    esp_now_send(broadcastMac, (unsigned char *)&p, 212);
  }
}

// Helper function to send String data (wraps the binary function)
void sendMeshMessage(uint16_t destId, String payload, uint8_t type = 0, uint8_t ttl = MAX_HOPS) {
  if (destId != 0 && type == 1)
    payload = ID + "=" + String(payload);
  int len = payload.length() + 1;
  if (len > 200) len = 200;

  sendMeshBinary(destId, (void*)payload.c_str(), len, type, ttl);
}

// ==========================================
// ESP-NOW RX/TX Callbacks
// ==========================================

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  // Minimal logic here
}

// CRITICAL FIX: ICACHE_RAM_ATTR forces this function into RAM.
// Without this, high traffic causes crashes when Flash is busy.
void ICACHE_RAM_ATTR onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  if (len != sizeof(MeshPacket)) return;

  int nextHead = (rxHead + 1) % RX_QUEUE_SIZE;
  if (nextHead == rxTail) {
    return;
  }

  memcpy(&rxQueue[rxHead], incomingData, len);
  rxHead = nextHead;
}

// ==========================================
// Mesh Processing (Main Loop)
// ==========================================

bool debugrecevie = false;

void processIncomingPackets(int iamConnected) {
  while (rxHead != rxTail) {

    MeshPacket packet = rxQueue[rxTail];
    rxTail = (rxTail + 1) % RX_QUEUE_SIZE;

    // 1. Magic/Network ID Check
    if (packet.magic != myConfig.meshId) {
      if (test_meshID == 0) {
        continue;
      } else if (test_meshID == 9999) {
        continue;
      } else if (packet.magic == test_meshID || packet.magic == 9998) {
        packet.sourceId = 999;
        packet.type = 1;
      } else {
        continue;
      }
    }

    // 2. Ignore My Own Packets (Echo from reflection)
    if (packet.sourceId == myConfig.nodeId) continue;
    if (packet.type == 3 || packet.sourceId == 1) espnowaval = 200;

    // Bounds check — a corrupt sourceId would otherwise index out of range below
    if (packet.sourceId >= MAX_NODES) {
      continue;
    }

    // --- Sequence-Based Deduplication ---
    uint16_t incomingSeq = packet.msgId & 0xFFFF;
    uint16_t lastSeq = nodeSequenceTracker[packet.sourceId];
    bool processPacket = false;

    if (incomingSeq > lastSeq) {
      processPacket = true;
    } else if ((lastSeq - incomingSeq) > REBOOT_THRESHOLD) {
      processPacket = true;
    }

    if (!processPacket && packet.magic != 9998) {
      continue;
    }

    nodeSequenceTracker[packet.sourceId] = incomingSeq;

    packet.payload[199] = '\0';   // payload[200] is out of bounds; valid indices are 0..199
    #ifdef debugmode
    Serial.printf("[RX] Src:%d -> Dst:%d | MsgID:%u | TTL:%d | Type:%d | Data:%s\n",
      packet.sourceId, packet.destId, packet.msgId, packet.ttl, packet.type, packet.payload);
    #endif

    // 3. Is it for me?
    if (packet.destId == myConfig.nodeId) {
      if (packet.type == 0) {
        memcpy(&nowdeviceData, packet.payload, sizeof(nowdeviceData));
        new_data = true;
        debugrecevie = true;
      } else if (packet.type == 1) {
        #ifdef debugmode
        debugdata(String("ESPNOW RECEIVED: " + String(packet.payload)).c_str());
        #endif
        decodeString2((const char *)packet.payload, 2);
      } else if (packet.type == 2) {
        memcpy(&nowdeviceData, packet.payload, sizeof(nowdeviceData));
        new_data = true;
        debugrecevie = true;
        sendMeshMessage(packet.sourceId, String(packet.msgId), 3);
      } else if (packet.type == 3) {
        if (retransmit == atoi(packet.payload)) {
          retransmit = 0;
        }
        delay(random(2, 5));
      }
    } else {
      // 4. Forwarding Logic
      if (packet.ttl > 0) {
        packet.ttl--;

        if (iamConnected) {
          if (packet.type == 0) {
            memcpy(&nowdeviceData, packet.payload, sizeof(nowdeviceData));
            new_data = true;
            debugrecevie = true;
            continue;
          }
        }
        if (packet.type == 1) {
          webSocket.broadcastTXT(packet.payload);
          decodeString2((const char *)packet.payload, 2);
        }

        delay(random(5, 15));
        esp_now_send(broadcastMac, (uint8_t *) &packet, sizeof(packet));
      } else {
        #ifdef debugmode
        Serial.println(" -> TTL Expired, Dropped");
        #endif
      }
    }
  }
}

// ==========================================
// Setup & Loop
// ==========================================

void espnow_setup() {
  loadSettings();
  if (myConfig.txTime < 2000) myConfig.txTime = 2000;

  if (myConfig.mesh_en) {
    if (esp_now_init() != 0) {
      #ifdef debugmode
      Serial.println("Error initializing ESP-NOW");
      #endif
      esp8266_now_active = false;
    } else {
      esp8266_now_active = true;
      esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
      esp_now_register_send_cb(onDataSent);
      esp_now_register_recv_cb(onDataRecv);
      if (myConfig.wifiChannel > 13) myConfig.wifiChannel = 1;
      esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, myConfig.wifiChannel, NULL, 0);
    }
  }

  server.on("/setID", []() {
    test_meshID = server.arg("mesh").toInt();
    server.send(200, "text/html", "OK");
  });
}

unsigned long lastTxTime = 0;

void espnow_loop() {
}

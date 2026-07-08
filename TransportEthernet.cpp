#include "TransportEthernet.h"
#include "MeshNowExports.h"
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <ENC28J60lwIP.h>

#define ETH_CS_PIN 15

static ENC28J60lwIP s_eth(ETH_CS_PIN);

void TransportEthernet::begin(const DeviceConfig &cfg) {
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setFrequency(4000000);

  IPAddress ip, gw, nm, dns1, dns2;
  dns1.fromString("8.8.8.8");
  dns2.fromString("8.8.4.4");
  if (ip.fromString(cfg.myIP) && gw.fromString(cfg.myGateway) && nm.fromString(cfg.myNetmask) && ip[0] > 0) {
    s_eth.config(ip, gw, nm, dns1, dns2);
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  mac[5] += 1;   // avoid colliding with the WiFi MAC on the same board

  if (s_eth.begin(mac)) {
    uint32_t startMs = millis();
    while (!s_eth.connected() && (millis() - startMs) < 30000) {
      delay(100);
      yield();
    }
    if (s_eth.connected()) {
      linked_ = true;
      ethercon = 1;
      WiFi.disconnect(true);   // Ethernet takes over completely, no WiFi STA fallback
    }
  }
}

void TransportEthernet::loop() {
  if ((millis() - lastLinkCheckMs_) < 2000) return;
  lastLinkCheckMs_ = millis();
  linked_ = s_eth.isLinked() && s_eth.connected();
  ethercon = linked_ ? 1 : 0;
}

void TransportEthernet::sendStatus(const StatusPayload &p) {
  String msg = "j" + String(p.deviceId) + "," + String(p.statusCode);
  sendToAll(msg.c_str(), 2);
}

bool TransportEthernet::isLinkUp() const {
  return linked_;
}

bool TransportEthernet::isNetworkUp() const {
  // No separate server-handshake concept modeled for this route — PHY link
  // is both tiers.
  return linked_;
}

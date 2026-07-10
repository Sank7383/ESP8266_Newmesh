#include "TransportEthernet.h"
#include "MeshNowExports.h"
#include <ESP8266WiFi.h>
#define USING_ENC28J60      true
#include <SPI.h>
#define CSPIN       15      // 5
#include <ENC28J60lwIP.h>
#define SHIELD_TYPE       "ESP8266_ENC28J60 Ethernet"
using TCPClient = WiFiClient;
#include <string.h>

// Same #define/#include set as the reference's #ifdef ether86 block
// (NewMeshN.h / NewMeshNOW.h) — added verbatim for exact parity per request.
// Confirmed by grepping the actual ENC28J60lwIP library sources
// (ENC28J60lwIP.h, enc28j60.h/.cpp, LwipIntfDev.h): none of USING_ENC28J60/
// SHIELD_TYPE/TCPClient are referenced there, so they don't change how the
// library behaves — they're carried over from the reference as-is, not
// because they're load-bearing.
#define ETH_CS_PIN CSPIN
// The proven-working reference (ethernetconnection(), the researched legacy
// firmware) never touches the ENC28J60/SPI during setup() — it defers the
// real hardware bring-up until ~7s after boot, called from myrun() (the
// main loop) via an ethchecknow timer, well after WiFi/AP/webserver setup
// has completed and settled. This class originally called SPI.begin()/
// s_eth.begin() synchronously and immediately from begin() (i.e. from
// .ino's setup(), before LocalAccessStack/AP/webserver were even fully
// wired up) — matched to the reference's timing here instead, since a pin-
// sharing theory for the "gets an IP but doesn't work / won't initialize"
// symptom was ruled out (confirmed on identical, fully-tested hardware).
#define ETH_INIT_DELAY_MS 7000

static ENC28J60lwIP s_eth(ETH_CS_PIN);

void TransportEthernet::begin(const DeviceConfig &cfg) {
  // Deliberately does nothing but stash the static-IP fields — see
  // ETH_INIT_DELAY_MS above and startHardware() below for why the actual
  // SPI/ENC28J60 work happens later, from loop().
  strlcpy(ip_, cfg.myIP, sizeof(ip_));
  strlcpy(gateway_, cfg.myGateway, sizeof(gateway_));
  strlcpy(netmask_, cfg.myNetmask, sizeof(netmask_));
}

void TransportEthernet::startHardware() {
  // Line-for-line port of the proven-working reference's ethernetconnection()
  // (NewMeshN.h) — same call order, same MAC-derivation, same 30s connect-
  // wait, same triple setDefault(). Serial trace points match its
  // "starging eth"/"no eth"/"eth chk"/"eth ok" checkpoints, but UNCONDITIONAL
  // here (the reference gated them behind #ifdef debugmode) so this is
  // observable on a serial monitor without needing a debug build — this is
  // the only way to tell "hardware init never ran" apart from "it ran and
  // failed" from outside the code.
  Serial.println("ETH: startHardware() running");

  uint8_t mac[6];
  WiFi.macAddress(mac);

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setFrequency(4000000);

  // lwIP supports multiple simultaneous interfaces (WiFi STA + Ethernet) on
  // ESP8266 — without explicitly marking this one the DEFAULT route, the
  // ENC28J60 can come up, link, and even get a DHCP/static IP while still
  // not actually being used for outbound traffic (DNS, TCP connections to
  // the server). The reference calls setDefault() three times: before
  // config, after config, and again once the link is confirmed — matched
  // exactly here rather than guessing which one call was load-bearing.
  s_eth.setDefault();

  IPAddress ip, gw, nm, dns1, dns2;
  dns1.fromString("8.8.8.8");
  dns2.fromString("8.8.4.4");
  if (ip.fromString(ip_) && gw.fromString(gateway_) && nm.fromString(netmask_) && ip[0] > 0) {
    s_eth.config(ip, gw, nm, dns1, dns2);
  }

  s_eth.setDefault();

  uint8_t etmac[6];
  for (int i = 0; i < 6; i++) etmac[i] = mac[i] + ((i == 5) ? 1 : 0);   // avoid colliding with the WiFi MAC on the same board

  Serial.println("ETH: starging eth (SPI/CS pin " + String(ETH_CS_PIN) + ")");
  if (!s_eth.begin(etmac)) {
    Serial.println("ETH: no eth - s_eth.begin() returned false, ENC28J60 not detected on SPI");
    linked_ = false;
    hwPresent_ = false;
    ethercon = 0;
    return;
  }
  hwPresent_ = true;

  uint32_t jmillis = millis();
  while (!s_eth.connected() && (millis() - jmillis) < 30000) {
    delay(100);
    yield();
  }
  Serial.println("ETH: eth chk - connected=" + String(s_eth.connected() ? 1 : 0) +
                  " waited=" + String(millis() - jmillis) + "ms");

  if (s_eth.connected()) {
    linked_ = true;
    ethercon = 1;
    WiFi.disconnect(true);   // Ethernet takes over completely, no WiFi STA fallback
  } else {
    linked_ = false;
    ethercon = 0;
  }
  s_eth.setDefault();

  // The WiFi route gets all of this for free from gotIpEventHandler
  // (NewMeshNOW.h) firing when STA gets an IP — Ethernet has no equivalent
  // event, so it has to be done explicitly here. Without this block, the
  // ENC28J60 could link and get an IP while the device never actually opens
  // a connection to the server at all — the second, bigger half of "gets an
  // IP but doesn't work".
  setup_AP(true);   // re-evaluate AP identity/subnet now that this device is bridged
  if (ethercon == 1) {
    Serial.println("ETH: eth ok - IP=" + s_eth.localIP().toString());
    amServer = AmServer();
    connectUplink();   // actually open the WebSocket/Socket.IO connection to myConfig.myServer
  }
}

void TransportEthernet::loop() {
  if (!hwInitAttempted_) {
    if (millis() < ETH_INIT_DELAY_MS) return;
    hwInitAttempted_ = true;
    startHardware();
    return;
  }
  // Never re-probe hardware that was never detected in the first place —
  // s_eth.isLinked() calls phyread(), which busy-waits on MISTAT_BUSY with
  // no yield(); with no chip on the SPI bus that bit reads as permanently
  // set and the wait never returns, eventually tripping the Soft WDT. See
  // hwPresent_'s declaration in TransportEthernet.h for the confirmed root
  // cause. Once begin() has failed, this route just stays down until reboot
  // (matches the reference's own behavior — it never retries eth.begin()
  // either after a failure, only reports ethercon=0 and falls back).
  if (!hwPresent_) return;
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
  // Now that begin() actually calls connectUplink() for this route, the
  // real uplink signal is available — mirror TransportMeshWifi::isLinkUp()
  // exactly instead of just returning the PHY link state, so the LED's
  // "server down" pink blink (LedStripController::setLinkStatus()) means
  // something for Ethernet too, rather than only ever reflecting the cable.
  return myConfig.socketio ? webSocketIo.isConnected() : webSocketClient.isConnected();
}

bool TransportEthernet::isNetworkUp() const {
  // The coarser tier — PHY link, independent of whether the server itself
  // is reachable.
  return linked_;
}

IPAddress ethernetLocalIP() {
  // s_eth.localIP() reads real lwIP netif state, not a cached member — safe
  // to call even before startHardware() has ever run (just returns 0.0.0.0).
  return s_eth.localIP();
}

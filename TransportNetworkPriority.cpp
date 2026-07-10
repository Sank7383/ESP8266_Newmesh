#include "TransportNetworkPriority.h"
#include "MeshNowExports.h"
#include <ESP8266WiFi.h>

void TransportNetworkPriority::begin(const DeviceConfig &cfg) {
  primaryIsEthernet_ = (cfg.networkType == (uint8_t)NetworkType::ETHERNET);
  fallbackEnabled_ = cfg.allowNetworkFallback;
  failoverMs_ = (uint32_t)((cfg.networkFailoverSec >= 10) ? cfg.networkFailoverSec : 30) * 1000UL;
  primaryStartMs_ = millis();
  fallbackTriggered_ = false;

  // Always started: espnow_setup() (local room-peer mesh) plus the shared
  // uplink-socket bookkeeping TransportMeshWifi::sendStatus()/isLinkUp()
  // use — both are interface-agnostic already (see class comment), so this
  // is correct whichever interface ends up actually carrying traffic.
  wifi_.begin(cfg);

  if (primaryIsEthernet_) {
    // Real ENC28J60 bring-up is still deferred ~7s internally (see
    // TransportEthernet) — begin() here only stashes the static-IP fields.
    eth_.begin(cfg);
  }
  // If WiFi is priority, its STA join of mySSID already happens
  // unconditionally from startconnection() (NewMeshNOW.h) at boot — nothing
  // extra to do here for that case.
}

void TransportNetworkPriority::loop() {
  wifi_.loop();
  // NOTE (Ethernet-priority path only): the first eth_.loop() call that
  // reaches ETH_INIT_DELAY_MS runs TransportEthernet::startHardware(),
  // which blocks synchronously for up to its own internal 30s connect-wait
  // before returning — so by the time control reaches the check below,
  // Ethernet's outcome is often already decided (success or failure) well
  // before networkFailoverSec elapses on its own. That's fine: the check
  // below is then just confirming what's already known, not the thing that
  // actually gated the wait. A short networkFailoverSec cannot make
  // Ethernet fail over FASTER than that ~7s+30s bound; it only matters as
  // configured for the WiFi-priority direction, which never blocks here.
  if (primaryIsEthernet_ || fallbackTriggered_) eth_.loop();

  if (!fallbackEnabled_ || fallbackTriggered_) return;

  bool primaryUp = primaryIsEthernet_ ? eth_.isNetworkUp() : (WiFi.status() == WL_CONNECTED);
  if (primaryUp) return;
  if ((millis() - primaryStartMs_) < failoverMs_) return;

  fallbackTriggered_ = true;
  if (primaryIsEthernet_) {
    debugdata("NETPRI: Ethernet did not link within networkFailoverSec - falling back to WiFi STA (mySSID)");
    if (String(myConfig.mySSID).length() > 3 && String(myConfig.myPass).length() > 3) {
      addAP(myConfig.mySSID, myConfig.myPass);
    } else {
      debugdata("NETPRI: fallback to WiFi requested but mySSID/myPass not configured - nothing to join");
    }
  } else {
    debugdata("NETPRI: WiFi did not associate within networkFailoverSec - falling back to Ethernet");
    eth_.begin(myConfig);   // first time this is called for this boot - stashes config, hwInitAttempted_ still false
  }
}

void TransportNetworkPriority::sendStatus(const StatusPayload &p) {
  // Drives the shared webSocketClient/webSocketIo uplink and the local
  // ESP-NOW room-peer broadcast — correct regardless of which physical
  // interface actually carries the IP traffic underneath (see class
  // comment), so this is always the one used here.
  wifi_.sendStatus(p);
}

bool TransportNetworkPriority::isLinkUp() const {
  return wifi_.isLinkUp();
}

bool TransportNetworkPriority::isNetworkUp() const {
  // TransportMeshWifi::isNetworkUp() == isConnected(), which already checks
  // the shared `ethercon` flag OR WiFi.status() — accurate regardless of
  // which member object actually brought the link up, so no need to
  // special-case eth_ here even when it's the one that's active.
  return wifi_.isNetworkUp();
}

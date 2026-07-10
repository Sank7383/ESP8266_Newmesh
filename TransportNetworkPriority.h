#pragma once
#include "ITransport.h"
#include "TransportMeshWifi.h"
#include "TransportEthernet.h"

// Selected by TransportFactory for RouteType::MESH_WIFI and
// RouteType::ETHERNET alike (RS485 is the only route that still gets its
// own distinct ITransport — it's a separate bus, not an IP interface). The
// actual WiFi-vs-Ethernet choice is made by myConfig.networkType (Form 1),
// not routeType — that field is authoritative regardless of what Form 11's
// Route Type dropdown says, per explicit requirement.
//
// Composes the two existing IP-network transports rather than duplicating
// their logic: TransportMeshWifi (ESP-NOW mesh + the shared WebSocket/
// Socket.IO uplink singletons) is always started, since local room-peer
// status sharing and the uplink socket layer are already interface-
// agnostic (webSocketClient/webSocketIo/isConnected() all key off the
// shared `ethercon` flag or WiFi.status(), not off which object called
// them). TransportEthernet is only started (its ENC28J60 bring-up is
// already deferred ~7s internally) when Ethernet is the priority
// interface, OR later, lazily, if fallback triggers.
//
// Fallback: if the priority interface hasn't reached isNetworkUp() within
// myConfig.networkFailoverSec of boot and myConfig.allowNetworkFallback is
// set, the OTHER interface's hardware is brought up as a second attempt.
// Once a fallback interface links, it stays active for the rest of this
// boot — matching the legacy firmware's own "Ethernet takes over
// completely" mutual-exclusion behavior rather than continuously racing
// the two.
class TransportNetworkPriority : public ITransport {
public:
  void begin(const DeviceConfig &cfg) override;
  void loop() override;
  void sendStatus(const StatusPayload &p) override;
  bool isLinkUp() const override;
  bool isNetworkUp() const override;

private:
  TransportMeshWifi wifi_;
  TransportEthernet eth_;
  bool primaryIsEthernet_ = false;
  bool fallbackEnabled_ = true;
  bool fallbackTriggered_ = false;
  uint32_t failoverMs_ = 30000;
  uint32_t primaryStartMs_ = 0;
};

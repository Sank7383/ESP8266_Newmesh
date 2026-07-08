#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"

struct StatusPayload {
  uint16_t deviceId;
  // The single number the server already understands (see
  // CallStateMachine::reportedStatusCode): 0 idle, 1 call, 2 care,
  // 3 extra-help, 4 code-blue, 5 toilet-call, 7 housekeeping,
  // 8 housekeeping+active-call, plus any site-configured custom code.
  uint8_t statusCode;
};

// Governs ONLY the status-uplink leg (device -> central server/gateway).
// The always-on local AP + config webserver + settings-websocket-server
// (LocalAccessStack) is deliberately NOT part of this interface — per the
// explicit requirement that local settings access stays available in every
// route variant, it runs unconditionally regardless of which ITransport is
// active.
class ITransport {
public:
  virtual void begin(const DeviceConfig &cfg) = 0;
  virtual void loop() = 0;
  virtual void sendStatus(const StatusPayload &p) = 0;
  // Is the actual server/uplink protocol (WebSocket/Socket.IO connected, or
  // RS485 bus being polled, or Ethernet link carrying traffic) confirmed up
  // right now? Drives the local LED's "server not connected" blink.
  virtual bool isLinkUp() const = 0;
  // Is the underlying network layer this route depends on up at all (WiFi
  // STA associated / Ethernet PHY linked)? This is a coarser, more
  // fundamental check than isLinkUp() — e.g. WiFi can be associated while
  // the uplink server itself is unreachable, but not the other way round.
  // Drives the local LED's "no WiFi/network" blink, which takes priority
  // over the "server not connected" blink. Routes with no separate network
  // tier below their own protocol (RS485) just mirror isLinkUp() here.
  virtual bool isNetworkUp() const = 0;
  virtual ~ITransport() = default;
};

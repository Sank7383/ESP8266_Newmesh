#pragma once
#include "ITransport.h"

// ENC28J60 SPI Ethernet bring-up with a static IP from
// myConfig.myIP/myGateway/myNetmask. Disables WiFi STA specifically (per
// the legacy firmware's mutual-exclusion behavior — Ethernet takes over
// completely rather than failing over) but leaves the always-on AP running,
// since LocalAccessStack owns that independently of route.
//
// Flagged in the plan for bench validation against real ENC28J60 shield
// hardware — translated from the researched legacy ether86 code path but
// unverified here, and isolated to this one file so a missing
// ENC28J60lwIP library only affects this translation unit.
class TransportEthernet : public ITransport {
public:
  void begin(const DeviceConfig &cfg) override;
  void loop() override;
  void sendStatus(const StatusPayload &p) override;
  bool isLinkUp() const override;
  bool isNetworkUp() const override;

private:
  bool linked_ = false;
  uint32_t lastLinkCheckMs_ = 0;
};

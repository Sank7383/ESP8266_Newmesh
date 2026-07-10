#pragma once
#include "ITransport.h"
#include <IPAddress.h>

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
  // begin() deliberately does NOT touch the ENC28J60/SPI — see startHardware()
  // in the .cpp for why. hwInitAttempted_ tracks whether loop() has done so yet.
  bool hwInitAttempted_ = false;
  // Set true only when s_eth.begin() itself succeeds (chip actually detected
  // on SPI). Once false, loop()'s periodic check must NOT call
  // s_eth.isLinked()/connected() again — isLinked() calls phyread(), which
  // busy-waits on MISTAT_BUSY with no yield(); with no chip on the bus that
  // bit never clears and the wait never returns, tripping the Soft WDT a few
  // seconds later. Confirmed against the ENC28J60lwIP library source, not a
  // guess — this is what caused the reboot when no shield is attached.
  bool hwPresent_ = false;
  uint32_t lastLinkCheckMs_ = 0;
  char ip_[16] = {0};
  char gateway_[16] = {0};
  char netmask_[16] = {0};

  void startHardware();
};

// Free function, not a method: the concrete TransportEthernet instance
// isn't reachable through the generic ITransport* the rest of the firmware
// holds (g_statusUplink) — this is how DeviceProtocol.cpp's Debug page
// (Form 14) gets at the actual assigned IP. Returns 0.0.0.0 whenever
// Ethernet isn't linked (not initialized yet, no cable, wrong route, etc.).
IPAddress ethernetLocalIP();

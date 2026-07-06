#pragma once
#include "ITransport.h"

// Half-duplex RS485 bus transport. This device acts as a polled slave: it
// listens on the shared bus for a poll byte addressed to its own
// machineid (high nibble match) and replies with a CRC16-framed status
// frame, matching the legacy firmware's bus protocol.
//
// NOTE: uses the hardware UART (Serial) for both TX and RX with a DE/RE
// direction pin toggled around writes — the more common/robust pattern for
// MAX485-style transceivers on ESP8266 — rather than the legacy firmware's
// SoftwareSerial-for-TX-only split. This route repurposes Serial entirely
// for the bus (matching the legacy firmware's own tradeoff), so no debug
// print output is available over USB while RS485 route is active.
//
// Flagged in the plan for bench validation against real bus/controller
// hardware — translated from the researched protocol but unverified here.
class TransportRs485 : public ITransport {
public:
  void begin(const DeviceConfig &cfg) override;
  void loop() override;
  void sendStatus(const StatusPayload &p) override;
  bool isLinkUp() const override;

private:
  static const uint8_t DIRECTION_PIN = 2;

  uint16_t machineId_ = 0;
  uint8_t pendingStatusCode_ = 0;   // 4 bits on the wire — see transmitFrame()
  uint32_t lastPollMs_ = 0;

  uint16_t crc16(const uint8_t *data, size_t len) const;
  void transmitFrame(uint8_t addrState);
};

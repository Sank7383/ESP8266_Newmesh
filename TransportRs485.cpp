#include "TransportRs485.h"

void TransportRs485::begin(const DeviceConfig &cfg) {
  machineId_ = (uint16_t)cfg.machineid;
  pendingStatusCode_ = 0;

  pinMode(DIRECTION_PIN, OUTPUT);
  digitalWrite(DIRECTION_PIN, LOW);   // idle = receive

  uint32_t baud = cfg.rs485Baud;
  if (baud != 9600 && baud != 4800 && baud != 19200) baud = 9600;
  Serial.begin(baud);
}

uint16_t TransportRs485::crc16(const uint8_t *data, size_t len) const {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
  }
  return crc;
}

void TransportRs485::transmitFrame(uint8_t addrState) {
  uint8_t frame[2] = { addrState, addrState };   // sent twice for noise immunity, matches legacy
  uint16_t crc = crc16(frame, sizeof(frame));

  digitalWrite(DIRECTION_PIN, HIGH);
  delay(2);
  Serial.write(frame, sizeof(frame));
  Serial.write((uint8_t)(crc >> 8));
  Serial.write((uint8_t)(crc & 0xFF));
  Serial.flush();
  delay(2);
  digitalWrite(DIRECTION_PIN, LOW);
}

void TransportRs485::loop() {
  while (Serial.available() >= 4) {
    uint8_t buf[4];
    for (uint8_t i = 0; i < 4; i++) buf[i] = (uint8_t)Serial.read();

    if (buf[0] != buf[1]) continue;   // the redundant pair must match

    uint16_t receivedCrc = ((uint16_t)buf[2] << 8) | buf[3];
    uint16_t computedCrc = crc16(buf, 2);
    if (receivedCrc != computedCrc) continue;   // corrupt frame, drop silently

    uint8_t pollAddr = (buf[0] >> 4) & 0x0F;
    if (pollAddr != (machineId_ & 0x0F)) continue;   // poll for a different node

    lastPollMs_ = millis();
    uint8_t addrState = (uint8_t)((machineId_ & 0x0F) << 4) | (pendingStatusCode_ & 0x0F);
    transmitFrame(addrState);
  }
}

void TransportRs485::sendStatus(const StatusPayload &p) {
  // The wire frame only has 4 bits for status (see transmitFrame); every
  // CallStateMachine::reportedStatusCode() value (0-8, plus custom codes)
  // fits comfortably within that range.
  pendingStatusCode_ = p.statusCode & 0x0F;
  // The actual bus write happens on the next poll addressed to us (loop());
  // RS485 here is a polled-slave protocol, not a push protocol.
}

bool TransportRs485::isLinkUp() const {
  return (millis() - lastPollMs_) < 60000UL;
}

bool TransportRs485::isNetworkUp() const {
  // No separate network tier below the bus protocol itself — being polled
  // recently IS the network being up for this route.
  return isLinkUp();
}

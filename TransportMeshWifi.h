#pragma once
#include "ITransport.h"

// Adapter over the vendored ESP-NOW mesh + WebSocket/Socket.IO uplink stack
// (MYESPNOW.h / NewMeshNOW.h, reached only via MeshNowExports.h — never
// re-included directly here, see that header's comment on single-TU
// inclusion). The always-on AP/webserver/STA-scan loop is owned by
// LocalAccessStack, not here; this class only drains the mesh RX queue and
// pushes status updates out.
class TransportMeshWifi : public ITransport {
public:
  void begin(const DeviceConfig &cfg) override;
  void loop() override;
  void sendStatus(const StatusPayload &p) override;
  bool isLinkUp() const override;
  bool isNetworkUp() const override;

private:
  UplinkProtocol uplink_ = UplinkProtocol::WEBSOCKET;
};

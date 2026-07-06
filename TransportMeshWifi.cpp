#include "TransportMeshWifi.h"
#include "MeshNowExports.h"

void TransportMeshWifi::begin(const DeviceConfig &cfg) {
  uplink_ = (UplinkProtocol)(cfg.socketio ? (uint8_t)UplinkProtocol::SOCKET_IO : (uint8_t)UplinkProtocol::WEBSOCKET);
  espnow_setup();

  if (uplink_ == UplinkProtocol::SOCKET_IO) {
    // Socket.IO cloud uplink is a fast-follow: the WebSocket path below
    // already reaches the local gateway/mesh server, and decodeString2()'s
    // dispatch already routes "rm_status_update"/"door_room_list" JSON the
    // same way regardless of transport. A dedicated SocketIOclient object
    // (see legacy webSocketIo/socketIOEvent) can be added here without
    // touching any other module once a socket.io endpoint is available to
    // test against.
    debugdata("INFO: Socket.IO uplink selected, falling back to WebSocket uplink for v1");
  }
}

void TransportMeshWifi::loop() {
  // Mesh RX draining is handled unconditionally by LocalAccessStack::loop()
  // (the RX queue is also fed by local WebSocket clients regardless of
  // route) — this only services the ESP-NOW-specific side of the stack.
  espnow_loop();
}

void TransportMeshWifi::sendStatus(const StatusPayload &p) {
  // "j<roomId>,<statusCode>" — the same single number the server already
  // understands (see CallStateMachine::reportedStatusCode), not a packed
  // pair of fields.
  String msg = "j" + String(p.deviceId) + "," + String(p.statusCode);
  sendMeshMessage(0, msg, 1);
  sendToAll(msg.c_str(), 2);
}

bool TransportMeshWifi::isLinkUp() const {
  return isConnected() || meshLayerActive();
}

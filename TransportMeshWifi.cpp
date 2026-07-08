#include "TransportMeshWifi.h"
#include "MeshNowExports.h"

void TransportMeshWifi::begin(const DeviceConfig &cfg) {
  uplink_ = (UplinkProtocol)(cfg.socketio ? (uint8_t)UplinkProtocol::SOCKET_IO : (uint8_t)UplinkProtocol::WEBSOCKET);
  espnow_setup();
  // The actual uplink connection (WebSocket to a mesh gateway or myServer,
  // or Socket.IO to myServer) is brought up by connectUplink() once STA
  // gets an IP (see NewMeshNOW.h's gotIpEventHandler) and kept alive by
  // uplinkHealthCheck() — nothing to do here if STA isn't connected yet.
}

void TransportMeshWifi::loop() {
  // Mesh RX draining and uplink health-checking are handled unconditionally
  // by LocalAccessStack::loop() (myrun()) regardless of route — this only
  // services the ESP-NOW-specific side of the stack.
  espnow_loop();
}

void TransportMeshWifi::sendStatus(const StatusPayload &p) {
  String rid = String(p.deviceId);
  String t = String(p.statusCode);

  // Local mesh-peer aggregation (feeds LedAggregator on other devices) is
  // independent of the external server link and always goes out.
  String localMsg = "j" + rid + "," + t;
  sendMeshMessage(0, localMsg, 1);

  // External server report — format matches the reference firmware's
  // merge_link() exactly: plain "s<id>,<state>,<doorid>" for a WebSocket/
  // Android-app server, or a Socket.IO "update_status" JSON event for a
  // Linux server.
  if (myConfig.socketio) {
    String json = "[\"update_status\",{\"asccode\":" + String(myConfig.asccode) +
                  ",\"r\":" + rid + ",\"door\":0,\"palm\":0,\"s\":" + t + ",\"drip\":0,\"type\":3}]";
    debugdata(String("MESHWIFI: SocketIO payload: " + json).c_str());
    if (webSocketIo.isConnected()) {
      webSocketIo.sendEVENT(json);
      debugdata("MESHWIFI: sent via SocketIO uplink");
    } else if (webSocketClient.isConnected() && isConnected()) {
      webSocketClient.sendTXT(json);
      debugdata("MESHWIFI: SocketIO not connected - sent JSON via WebSocket fallback");
    } else {
      debugdata("MESHWIFI: no uplink connected - server will NOT receive this update, will auto-retry");
    }
  } else {
    String msg = "s" + rid + "," + t + "," + String(myConfig.doorIndicatorId);
    debugdata(String("MESHWIFI: WebSocket payload: " + msg).c_str());
    if (webSocketClient.isConnected() && isConnected()) {
      webSocketClient.sendTXT(msg);
      debugdata("MESHWIFI: sent to uplink server");
    } else {
      debugdata("MESHWIFI: uplink not connected - server will NOT receive this update, will auto-retry");
    }
    sendToAll(msg.c_str(), 1);
  }
}

bool TransportMeshWifi::isLinkUp() const {
  // A permissive "is any uplink mechanism up" signal for status/debug
  // display; sendStatus() above checks the specific relevant client itself
  // when it actually matters for delivery.
  return webSocketClient.isConnected() || webSocketIo.isConnected() || meshLayerActive();
}

#include "LocalAccessStack.h"
#include "MeshNowExports.h"

void LocalAccessStack::begin(const DeviceConfig &cfg) {
  startconnection();
}

void LocalAccessStack::loop() {
  myrun();
  // Drained unconditionally (not just on the WiFi-mesh route) because the
  // RX ring buffer is also fed by WStype_BIN frames arriving on the
  // always-on local AP's WebSocket server, regardless of which ITransport
  // is active.
  processIncomingPackets(isConnected() ? 1 : 0);
}

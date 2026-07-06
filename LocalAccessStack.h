#pragma once
#include "NurseCallConfig.h"

// Always-on local AP + STA + config webserver + settings-websocket-server,
// independent of which ITransport is active. Thin wrapper over
// startconnection()/myrun() (NewMeshNOW.h, reached via MeshNowExports.h) —
// those already implement AP bring-up (single "NETE"+asccode+machineid
// identity, see setup_AP() in NewMeshNOW.h), the WiFi STA multi-AP scan/
// connect state machine (skipped automatically once TransportEthernet sets
// the ethercon flag), and the webserver/websocket-server loop.
class LocalAccessStack {
public:
  void begin(const DeviceConfig &cfg);
  void loop();
};

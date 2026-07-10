#include "TransportFactory.h"
#include "TransportRs485.h"
#include "TransportNetworkPriority.h"

namespace {
  TransportRs485 s_rs485;
  TransportNetworkPriority s_networkPriority;
}

namespace TransportFactory {
  ITransport* create(const DeviceConfig &cfg) {
    // RS485 is the only route that still gets its own distinct transport —
    // it's a separate bus, not an IP interface. MESH_WIFI and ETHERNET are
    // now equivalent here: both route to TransportNetworkPriority, which
    // decides WiFi vs Ethernet itself from myConfig.networkType (Form 1,
    // authoritative regardless of this Route Type setting) and falls back
    // to the other interface if the priority one doesn't come up in time.
    switch ((RouteType)cfg.routeType) {
      case RouteType::RS485:
        return &s_rs485;
      case RouteType::MESH_WIFI:
      case RouteType::ETHERNET:
      default:
        return &s_networkPriority;
    }
  }
}

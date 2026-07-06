#include "TransportFactory.h"
#include "TransportMeshWifi.h"
#include "TransportRs485.h"
#include "TransportEthernet.h"

namespace {
  TransportMeshWifi s_meshWifi;
  TransportRs485 s_rs485;
  TransportEthernet s_ethernet;
}

namespace TransportFactory {
  ITransport* create(const DeviceConfig &cfg) {
    switch ((RouteType)cfg.routeType) {
      case RouteType::RS485:
        return &s_rs485;
      case RouteType::ETHERNET:
        return &s_ethernet;
      case RouteType::MESH_WIFI:
      default:
        return &s_meshWifi;
    }
  }
}

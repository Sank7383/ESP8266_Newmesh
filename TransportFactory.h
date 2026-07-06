#pragma once
#include "ITransport.h"

namespace TransportFactory {
  ITransport* create(const DeviceConfig &cfg);
}

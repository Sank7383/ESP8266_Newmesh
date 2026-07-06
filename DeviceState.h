#pragma once
#include <Arduino.h>
#include "CallStateMachine.h"
#include "LedAggregator.h"

// Shared, non-persisted runtime state, owned/defined by the composition
// root (ESP8266_Newmesh.ino) and referenced by DeviceProtocol.cpp so the
// mesh command dispatcher can report/aggregate current call status without
// each module needing its own copy.
extern CallStateMachine::CallStatus g_callStatus;
extern LedAggregator g_roomAggregator;

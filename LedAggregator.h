#pragma once
#include <Arduino.h>
#include "NurseCallConfig.h"

// Multi-room priority aggregation (room_status[]-equivalent), kept as a
// distinct concern from the per-device CallStateMachine — this only feeds
// the aggregate LED zone (door-indicator whole-strip color, or a bed unit's
// trailing "site status" LEDs), it never influences this device's own call
// legality.
class LedAggregator {
public:
  void begin() { count_ = 0; }

  void updateRoom(uint16_t roomId, uint8_t status) {
    for (uint8_t i = 0; i < count_; i++) {
      if (roomIds_[i] == roomId) { statuses_[i] = status; return; }
    }
    if (count_ < MAX_ROOMS) {
      roomIds_[count_] = roomId;
      statuses_[count_] = status;
      count_++;
    }
  }

  void bulkUpdate(const uint16_t *roomIds, const uint8_t *statuses, uint8_t n) {
    count_ = 0;
    for (uint8_t i = 0; i < n && count_ < MAX_ROOMS; i++) {
      roomIds_[count_] = roomIds[i];
      statuses_[count_] = statuses[i];
      count_++;
    }
  }

  const uint8_t* roomStates() const { return statuses_; }
  uint8_t count() const { return count_; }

private:
  uint16_t roomIds_[MAX_ROOMS];
  uint8_t statuses_[MAX_ROOMS];
  uint8_t count_ = 0;
};

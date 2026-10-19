#pragma once

#include <cstdint>

struct SyncSettings {
  bool auto_sync = true;
  uint8_t source = 0;
};

struct HostSettings {
  SyncSettings sync;

  const SyncSettings &getSync() const { return sync; }
};

extern HostSettings settings;

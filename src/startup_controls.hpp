#pragma once
#include "ardustereo/controls.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ardustereo { namespace detail {
inline void startup_valid(const ControlRange& r, int value) {
  if (r.minimum > r.maximum || r.step <= 0 || value < r.minimum || value > r.maximum ||
      (static_cast<std::int64_t>(value) - r.minimum) % r.step)
    throw std::invalid_argument("Startup control outside driver range/step");
}
inline int startup_neighbour(const ControlRange& r, int value) {
  const auto up = static_cast<std::int64_t>(value) + r.step;
  if (up <= r.maximum) return static_cast<int>(up);
  const auto down = static_cast<std::int64_t>(value) - r.step;
  if (down >= r.minimum) return static_cast<int>(down);
  throw std::runtime_error("Startup sensor control has no legal adjacent value");
}

// Used after stream-on and before warmup/publication, never for runtime changes.
// Equal S_CTRL may retain a stale hardware default behind a V4L2 cache. A legal
// adjacent value followed by the target makes that final write change the cache.
inline ControlState initialize_startup_controls(ControlTransport& t, const std::array<int,3>& requested) {
  ControlState before; before.values={{-1,-1,-1}};
  std::array<int,3> adjacent{{0,0,0}}, restore_adjacent{{0,0,0}};
  for (unsigned i=0; i<3; ++i) {
    if (requested[i]==-1) continue;
    before.ranges[i]=t.query(i); before.values[i]=t.read(i);
    startup_valid(before.ranges[i],requested[i]); startup_valid(before.ranges[i],before.values[i]);
    if (i) {
      adjacent[i]=startup_neighbour(before.ranges[i],requested[i]);
      restore_adjacent[i]=startup_neighbour(before.ranges[i],before.values[i]);
    }
  }
  bool attempted=false;
  try {
    for (unsigned i=0; i<3; ++i) {
      if (requested[i]==-1) continue;
      attempted=true;
      if (i) t.write(i,adjacent[i]);
      t.write(i,requested[i]);
    }
    auto after=before;
    for (unsigned i=0; i<3; ++i) {
      if (requested[i]==-1) continue;
      after.values[i]=t.read(i);
      if (after.values[i]!=requested[i]) throw std::runtime_error("Startup control readback differs from request");
    }
    return after;
  } catch (const std::exception& error) {
    std::string failures;
    // Restore nominal readbacks, not unknown sensor registers. FPS comes first
    // because setting it can reset sensor state. Source closes on failure.
    if (attempted) {
      for (unsigned i=0; i<3; ++i) {
        if (requested[i]==-1) continue;
        bool failed=false;
        if (i) { try { t.write(i,restore_adjacent[i]); } catch (...) { failed=true; } }
        try { t.write(i,before.values[i]); } catch (...) { failed=true; }
        if (failed) failures += " startup_rollback_failed_index="+std::to_string(i);
      }
      for (unsigned i=0; i<3; ++i) {
        if (requested[i]==-1) continue;
        try { if (t.read(i)!=before.values[i]) throw std::runtime_error("restore mismatch"); }
        catch (...) { failures += " startup_rollback_verify_failed_index="+std::to_string(i); }
      }
    }
    throw std::runtime_error(std::string(error.what())+failures);
  }
}
} } // namespace ardustereo::detail

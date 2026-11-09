#pragma once
#include "ardustereo/config.hpp"
#include <string>

namespace ardustereo {
inline bool has_startup_controls(const CaptureConfig& c) {
  return c.frame_rate != -1 || c.exposure != -1 || c.analogue_gain != -1;
}
// Called after OpenCV starts streaming, before warmup/publication.
std::string apply_startup_controls(const CaptureConfig& config);
}  // namespace ardustereo

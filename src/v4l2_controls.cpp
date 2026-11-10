#include "v4l2_controls.hpp"
#include "startup_controls.hpp"
#include <sstream>
#include <stdexcept>

namespace ardustereo {
std::string apply_startup_controls(const CaptureConfig& config) {
  if (!has_startup_controls(config)) return "controls=not_requested";
#ifndef __linux__
  throw std::runtime_error("Camera controls require Linux V4L2");
#else
  auto transport=make_linux_controls(config.device);
  const auto state=detail::initialize_startup_controls(*transport,
      {{config.frame_rate,config.exposure,config.analogue_gain}});
  std::ostringstream result;
  result << "controls_verified startup_sensor_initialization=adjacent_then_target";
  const char* names[]={"frame_rate","exposure","analogue_gain"};
  for (unsigned i=0; i<3; ++i)
    if (state.values[i]!=-1) result << ' ' << names[i] << '=' << state.values[i];
  return result.str();
#endif
}
} // namespace ardustereo

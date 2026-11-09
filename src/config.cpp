#include "ardustereo/config.hpp"
#include "ardustereo/frame_ops.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardustereo {

void validate_config(const CaptureConfig& c) {
  if (c.device.empty()) throw std::invalid_argument("device must not be empty");
  if (c.width <= 0 || c.height <= 0 || c.split_column <= 0 || c.split_column >= c.width)
    throw std::invalid_argument("Invalid dimensions or split_column");
  checked_product(static_cast<std::size_t>(c.width), static_cast<std::size_t>(c.height));
  if (c.warmup_frames < 0 || c.warmup_frames > 1000 || c.buffer_size < 1 || c.buffer_size > 64)
    throw std::invalid_argument("warmup_frames must be [0,1000], buffer_size [1,64]");
  if (c.input_format != "mono8" && c.input_format != "mono16")
    throw std::invalid_argument("input_format must be mono8 or mono16");
  if (c.input_format == "mono8" && c.mono16_shift != -1)
    throw std::invalid_argument("mono16_shift must be -1 for mono8 input");
  if (c.input_format == "mono16" && (c.mono16_shift < 0 || c.mono16_shift > 8))
    throw std::invalid_argument("mono16 input requires mono16_shift in [0,8]");
  if (!c.fourcc.empty() && c.fourcc.size() != 4)
    throw std::invalid_argument("fourcc must be empty or exactly four characters");
  if (c.frame_rate == 0 || c.frame_rate < -1 || c.exposure == 0 || c.exposure < -1 ||
      c.analogue_gain == 0 || c.analogue_gain < -1)
    throw std::invalid_argument("frame_rate, exposure and analogue_gain require positive raw integers or -1 (unset)");
}

void validate_calibration(const Calibration& c, int width, int height, bool required) {
  if (width <= 0 || height <= 0 || c.width != width || c.height != height)
    throw std::invalid_argument("Calibration dimensions do not match the image");
  const auto finite = [](double x) { return std::isfinite(x); };
  if (!std::all_of(c.k.begin(), c.k.end(), finite) ||
      !std::all_of(c.r.begin(), c.r.end(), finite) ||
      !std::all_of(c.p.begin(), c.p.end(), finite) ||
      !std::all_of(c.d.begin(), c.d.end(), finite))
    throw std::invalid_argument("Calibration contains non-finite values");
  if (c.k[0] == 0.0) {
    if (required) throw std::invalid_argument("Calibration is required but unavailable");
    const auto zero = [](double x) { return x == 0.0; };
    if (!std::all_of(c.k.begin(), c.k.end(), zero) ||
        !std::all_of(c.r.begin(), c.r.end(), zero) ||
        !std::all_of(c.p.begin(), c.p.end(), zero) ||
        !std::all_of(c.d.begin(), c.d.end(), zero))
      throw std::invalid_argument("Uncalibrated CameraInfo must have zero D/K/R/P");
    return;
  }
  if (c.k[0] <= 0 || c.k[4] <= 0 || std::abs(c.k[8] - 1.0) > 1e-6 ||
      c.p[0] <= 0 || c.p[5] <= 0 || std::abs(c.p[10] - 1.0) > 1e-6)
    throw std::invalid_argument("Invalid calibration intrinsic/projection matrix");
  const auto n = c.d.size();
  const bool model_ok = (c.distortion_model == "plumb_bob" && n == 5) ||
      (c.distortion_model == "equidistant" && n == 4) ||
      (c.distortion_model == "rational_polynomial" && (n == 8 || n == 12 || n == 14));
  if (!model_ok) throw std::invalid_argument("Unsupported distortion model or coefficient count");
  const double det = c.r[0] * (c.r[4] * c.r[8] - c.r[5] * c.r[7]) -
      c.r[1] * (c.r[3] * c.r[8] - c.r[5] * c.r[6]) +
      c.r[2] * (c.r[3] * c.r[7] - c.r[4] * c.r[6]);
  if (std::abs(det - 1.0) > 1e-3)
    throw std::invalid_argument("Calibration rectification matrix is not a rotation");
  for (int a = 0; a < 3; ++a)
    for (int b = 0; b < 3; ++b) {
      double dot = 0;
      for (int i = 0; i < 3; ++i) dot += c.r[3 * a + i] * c.r[3 * b + i];
      if (std::abs(dot - (a == b ? 1.0 : 0.0)) > 1e-3)
        throw std::invalid_argument("Calibration rectification matrix is not orthonormal");
    }
}

}  // namespace ardustereo

#pragma once

#include <array>
#include <string>
#include <vector>

namespace ardustereo {

struct CaptureConfig {
  std::string device = "/dev/video0";
  int width = 2560;
  int height = 800;
  int split_column = 1280;
  int warmup_frames = 5;
  int buffer_size = 1;
  std::string input_format = "mono8";
  int mono16_shift = -1;
  // Empty accepts OpenCV negotiation; the B0266 profile explicitly requests GREY.
  std::string fourcc;
  // Driver raw integers. -1 means do not request this hardware control.
  int frame_rate = -1;
  int exposure = -1;
  int analogue_gain = -1;
};

void validate_config(const CaptureConfig& config);

// ROS-independent calibration contract, not a proof of physical validity.
struct Calibration {
  int width = 0;
  int height = 0;
  std::string distortion_model;
  std::vector<double> d;
  std::array<double, 9> k{};
  std::array<double, 9> r{};
  std::array<double, 12> p{};
};

void validate_calibration(const Calibration& calibration, int width, int height,
                          bool require_calibration);

}  // namespace ardustereo

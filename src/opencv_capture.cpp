// Derived from the original mono_cam_cpp_node.cpp acquisition path.
// Copyright (c) 2026 Chengdong Zhu and contributors. SPDX-License-Identifier: MIT
#include "ardustereo/opencv_capture.hpp"
#include "v4l2_controls.hpp"

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ardustereo {
namespace {

std::string fourcc_string(double value) {
  if (!std::isfinite(value) || value <= 0) return "unavailable";
  const auto code = static_cast<std::uint32_t>(value);
  std::string out(4, '?');
  for (unsigned i = 0; i < 4; ++i) {
    const char c = static_cast<char>((code >> (8 * i)) & 255U);
    out[i] = c >= 32 && c <= 126 ? c : '?';
  }
  return out;
}

class OpenCvV4l2Source final : public CaptureSource {
 public:
  void open(const CaptureConfig& config) override {
    validate_config(config);
#ifndef __linux__
    (void)config;
    throw std::runtime_error("The camera backend requires Linux V4L2");
#else
    close();
    config_ = config;
    counter_ = 0;
    try {
      if (!cap_.open(config.device, cv::CAP_V4L2))
        throw std::runtime_error("Cannot open V4L2 device: " + config.device);
      if (!config.fourcc.empty()) {
        const int requested = cv::VideoWriter::fourcc(config.fourcc[0], config.fourcc[1],
                                                     config.fourcc[2], config.fourcc[3]);
        if (!cap_.set(cv::CAP_PROP_FOURCC, requested))
          throw std::runtime_error("OpenCV rejected the requested FourCC");
        if (fourcc_string(cap_.get(cv::CAP_PROP_FOURCC)) != config.fourcc)
          throw std::runtime_error("FourCC readback differs from request");
      }
      const bool width_set = cap_.set(cv::CAP_PROP_FRAME_WIDTH, config.width);
      const bool height_set = cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config.height);
      const bool buffer_set = cap_.set(cv::CAP_PROP_BUFFERSIZE, config.buffer_size);
      if (!cap_.set(cv::CAP_PROP_CONVERT_RGB, 0))
        throw std::runtime_error("Cannot disable OpenCV RGB conversion; refusing an ambiguous format");
      const double width = cap_.get(cv::CAP_PROP_FRAME_WIDTH);
      const double height = cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
      if (!width_set || !height_set || !std::isfinite(width) || !std::isfinite(height) ||
          std::abs(width - config.width) > 0.1 || std::abs(height - config.height) > 0.1)
        throw std::runtime_error("V4L2 dimensions were rejected or differ from the request");
      if (!config.fourcc.empty() && fourcc_string(cap_.get(cv::CAP_PROP_FOURCC)) != config.fourcc)
        throw std::runtime_error("FourCC changed while applying the requested image dimensions");
      std::string controls = "controls=not_requested";
      if (has_startup_controls(config)) {
        // Match the established workflow: stream first, then apply controls.
        // Discard this initial frame; CaptureSession performs configured warmup next.
        cv::Mat initial;
        if (!cap_.read(initial) || initial.empty())
          throw std::runtime_error("Cannot start camera stream before applying controls");
        controls = apply_startup_controls(config);
      }
      std::ostringstream s;
      s << "OpenCV CAP_V4L2 device=" << config.device << " requested=" << config.width
        << 'x' << config.height << " readback=" << width << 'x' << height
        << " fourcc=" << fourcc_string(cap_.get(cv::CAP_PROP_FOURCC))
        << " buffer_request=" << config.buffer_size << " buffer_set_accepted=" << buffer_set
        << " buffer_property_readback=" << cap_.get(cv::CAP_PROP_BUFFERSIZE)
        << " driver_sequence=unavailable " << controls;
      description_ = s.str();
    } catch (...) {
      close();
      throw;
    }
#endif
  }

  bool read(CapturedFrame& frame) override {
    frame = CapturedFrame{};
    if (!cap_.isOpened()) throw std::logic_error("V4L2 source is closed");
    cv::Mat mat;
    if (!cap_.read(mat) || mat.empty()) return false;
    const auto receive_time = system_time_ns();
    ImageView view;
    view.data = mat.data;
    view.bytes = static_cast<std::size_t>(mat.dataend - mat.data);
    view.width = mat.cols;
    view.height = mat.rows;
    view.stride = mat.step[0];
    if (config_.input_format == "mono8" && mat.type() == CV_8UC1) {
      view.format = PixelFormat::Mono8;
    } else if (config_.input_format == "mono16" && mat.type() == CV_16UC1) {
      const std::uint16_t one = 1;
      view.format = *reinterpret_cast<const std::uint8_t*>(&one) == 1
          ? PixelFormat::Mono16LE : PixelFormat::Mono16BE;
    } else {
      throw FrameValidationError("Unexpected Mat type; only the explicitly configured mono format is accepted");
    }
    try { frame.image = normalize_mono8(view, config_.mono16_shift); }
    catch (const std::exception& e) { throw FrameValidationError(e.what()); }
    frame.received_system_ns = receive_time;
    frame.source_counter = counter_++;
    return true;
  }

  void close() noexcept override {
    try { cap_.release(); } catch (...) { /* Destruction must not throw. */ }
  }
  std::string description() const override { return description_; }

 private:
  cv::VideoCapture cap_;
  CaptureConfig config_;
  std::uint64_t counter_ = 0;
  std::string description_ = "OpenCV V4L2 source (closed)";
};
}  // namespace

std::unique_ptr<CaptureSource> make_opencv_v4l2_source() {
  return std::unique_ptr<CaptureSource>(new OpenCvV4l2Source);
}
}  // namespace ardustereo

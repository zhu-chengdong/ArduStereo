#include "ardustereo/capture.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace ardustereo {

std::uint64_t system_time_ns() {
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (ns <= 0) throw std::runtime_error("Host system clock is invalid");
  return static_cast<std::uint64_t>(ns);
}

CaptureSession::CaptureSession(std::unique_ptr<CaptureSource> source) : source_(std::move(source)) {
  if (!source_) throw std::invalid_argument("Capture source must not be null");
}
CaptureSession::~CaptureSession() { close(); }

void CaptureSession::open(const CaptureConfig& config) {
  if (open_) throw std::logic_error("Capture session is already open");
  validate_config(config);
  config_ = config;
  try {
    source_->open(config);
    open_ = true;
    for (int i = 0; i < config.warmup_frames; ++i) {
      CapturedFrame frame;
      if (!read(frame)) throw std::runtime_error("Capture warmup failed");
    }
  } catch (...) {
    source_->close();
    open_ = false;
    throw;
  }
}

bool CaptureSession::read(CapturedFrame& frame) {
  if (!open_) throw std::logic_error("Capture session is closed");
  frame = CapturedFrame{};
  CapturedFrame next;
  if (!source_->read(next)) return false;
  try {
    validate_image(next.image);
    if (next.image.width != config_.width || next.image.height != config_.height)
      throw std::runtime_error("Captured dimensions differ from the configured dimensions");
    if (next.received_system_ns == 0) throw std::runtime_error("Source did not provide a host receive time");
  } catch (const std::exception& error) {
    throw FrameValidationError(error.what());
  }
  frame = std::move(next);
  return true;
}

void CaptureSession::close() noexcept {
  if (open_) source_->close();
  open_ = false;
}

std::string CaptureSession::description() const { return source_->description(); }

namespace {
class SyntheticSource final : public CaptureSource {
 public:
  void open(const CaptureConfig& config) override {
    validate_config(config);
    if (config.input_format != "mono8" || !config.fourcc.empty())
      throw std::invalid_argument("Synthetic source supports packed mono8 without a FourCC override");
    if (config.frame_rate != -1 || config.exposure != -1 || config.analogue_gain != -1)
      throw std::invalid_argument("Hardware controls require source=v4l2; synthetic input cannot apply them");
    config_ = config;
    counter_ = 0;
    open_ = true;
  }
  bool read(CapturedFrame& frame) override {
    if (!open_) throw std::logic_error("Synthetic source is closed");
    frame = CapturedFrame{};
    frame.received_system_ns = system_time_ns();
    frame.source_counter = counter_++;
    frame.image.width = config_.width;
    frame.image.height = config_.height;
    frame.image.data.resize(checked_product(static_cast<std::size_t>(config_.width),
                                            static_cast<std::size_t>(config_.height)));
    for (int y = 0; y < config_.height; ++y)
      for (int x = 0; x < config_.width; ++x)
        frame.image.data[static_cast<std::size_t>(y) * config_.width + x] =
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(x) +
                17ULL * static_cast<std::uint64_t>(y) + frame.source_counter) % 256);
    return true;
  }
  void close() noexcept override { open_ = false; }
  std::string description() const override { return "SYNTHETIC TEST SOURCE (no camera)"; }
 private:
  CaptureConfig config_;
  std::uint64_t counter_ = 0;
  bool open_ = false;
};
}  // namespace

std::unique_ptr<CaptureSource> make_synthetic_source() {
  return std::unique_ptr<CaptureSource>(new SyntheticSource);
}

}  // namespace ardustereo

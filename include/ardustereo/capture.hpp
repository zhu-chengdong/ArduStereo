#pragma once

#include "ardustereo/config.hpp"
#include "ardustereo/frame_ops.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace ardustereo {

class FrameValidationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct CapturedFrame {
  MonoImage image;
  std::uint64_t received_system_ns = 0;
  std::uint64_t source_counter = 0;  // Application/synthetic count, NOT a driver sequence.
};

class CaptureSource {
 public:
  virtual ~CaptureSource() = default;
  virtual void open(const CaptureConfig& config) = 0;
  virtual bool read(CapturedFrame& frame) = 0;
  virtual void close() noexcept = 0;
  virtual std::string description() const = 0;
};

class CaptureSession {
 public:
  explicit CaptureSession(std::unique_ptr<CaptureSource> source);
  ~CaptureSession();
  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;
  void open(const CaptureConfig& config);
  bool read(CapturedFrame& frame);
  void close() noexcept;
  std::string description() const;
 private:
  std::unique_ptr<CaptureSource> source_;
  CaptureConfig config_;
  bool open_ = false;
};

// Explicit test/demo source. It never opens a device and is never an automatic fallback.
std::unique_ptr<CaptureSource> make_synthetic_source();
std::uint64_t system_time_ns();

}  // namespace ardustereo

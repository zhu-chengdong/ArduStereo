#pragma once

#include "ardustereo/capture.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace ardustereo {

struct DiagnosticConfig {
  double window_seconds = 5.0;
  double stale_after_seconds = 2.0;
  double expected_fps = 0.0;  // Monitoring reference only; never sets camera FPS.
  double minimum_fps_ratio = 0.9;
};

struct DiagnosticSnapshot {
  std::string state = "starting";
  std::string detail;
  std::string error_stage;
  unsigned level = 1;  // ROS-compatible OK/WARN/ERROR/STALE; core has no ROS dependency.
  std::uint64_t read_attempts = 0, captured_frames = 0, published_pairs = 0;
  std::uint64_t composite_publications = 0, empty_reads = 0, consecutive_empty_reads = 0;
  std::uint64_t frame_rejections = 0, read_exceptions = 0;
  double uptime_seconds = 0.0, last_frame_age_seconds = -1.0;
  double capture_fps = 0.0, publication_fps = 0.0;
  double capture_window_seconds = 0.0, publication_window_seconds = 0.0;
  bool read_in_progress = false, window_limited = false;
};

std::uint64_t steady_time_ns();
void validate_diagnostic_config(const DiagnosticConfig& config);

// Clock is read under the mutex. A test clock can advance without wall-clock sleeps.
class RuntimeDiagnostics {
 public:
  using Clock = std::function<std::uint64_t()>;
  explicit RuntimeDiagnostics(DiagnosticConfig config = {}, Clock clock = steady_time_ns);
  void ready();
  void read_started();
  void frame_received();
  void empty_read();
  void rejected_frame();
  void read_exception();
  void pair_published(bool composite);
  void fail(const std::string& stage, const std::string& message);
  void stop();
  DiagnosticSnapshot snapshot();
 private:
  struct Window {
    std::deque<std::uint64_t> events;
    std::uint64_t retained_since = 0;
    void add(std::uint64_t now);
    double rate(std::uint64_t now, std::uint64_t start, std::uint64_t horizon,
                double& seconds, bool& limited);
  };
  std::uint64_t now_locked();
  void require_running() const;
  DiagnosticConfig config_;
  Clock clock_;
  std::mutex mutex_;
  DiagnosticSnapshot values_;
  std::uint64_t started_ = 0, ready_ = 0, last_clock_ = 0, last_frame_ = 0, terminal_ = 0;
  bool is_ready_ = false, has_frame_ = false, terminal_state_ = false;
  Window captures_, publications_;
};

// Shared production/test orchestration. Warmup stays inside CaptureSession and is
// excluded from runtime counters. Terminal errors preserve their classification.
class MonitoredCapture {
 public:
  MonitoredCapture(std::unique_ptr<CaptureSource> source, RuntimeDiagnostics& diagnostics,
                   unsigned max_empty_reads);
  void open(const CaptureConfig& config);
  bool read(CapturedFrame& frame);
  void close();
  std::string description() const;
 private:
  CaptureSession session_;
  RuntimeDiagnostics& diagnostics_;
  unsigned maximum_, consecutive_ = 0;
};

}  // namespace ardustereo

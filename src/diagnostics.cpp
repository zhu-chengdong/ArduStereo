#include "ardustereo/diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ardustereo {
namespace {
constexpr double billion = 1000000000.0;
constexpr std::size_t maximum_samples = 4096;
std::uint64_t nanoseconds(double seconds) {
  return static_cast<std::uint64_t>(seconds * billion);
}
}  // namespace

std::uint64_t steady_time_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void validate_diagnostic_config(const DiagnosticConfig& c) {
  if (!std::isfinite(c.window_seconds) || c.window_seconds < 0.1 || c.window_seconds > 300 ||
      !std::isfinite(c.stale_after_seconds) || c.stale_after_seconds < 0.05 ||
      c.stale_after_seconds > 300 || !std::isfinite(c.expected_fps) ||
      c.expected_fps < 0 || c.expected_fps > 10000 ||
      !std::isfinite(c.minimum_fps_ratio) || c.minimum_fps_ratio <= 0 || c.minimum_fps_ratio > 1)
    throw std::invalid_argument("Invalid diagnostic window, stale timeout or FPS reference");
}

RuntimeDiagnostics::RuntimeDiagnostics(DiagnosticConfig config, Clock clock)
    : config_(config), clock_(std::move(clock)) {
  validate_diagnostic_config(config_);
  if (!clock_) throw std::invalid_argument("Diagnostic clock must not be empty");
  started_ = last_clock_ = clock_();
}

std::uint64_t RuntimeDiagnostics::now_locked() {
  const auto now = clock_();
  if (now < last_clock_) throw std::logic_error("Diagnostic monotonic clock moved backwards");
  last_clock_ = now;
  return now;
}
void RuntimeDiagnostics::require_running() const {
  if (!is_ready_ || terminal_state_) throw std::logic_error("Diagnostic session is not running");
}
void RuntimeDiagnostics::Window::add(std::uint64_t now) {
  events.push_back(now);
  if (events.size() > maximum_samples) {
    retained_since = events.front();
    events.pop_front();
  }
}
double RuntimeDiagnostics::Window::rate(std::uint64_t now, std::uint64_t start,
                                      std::uint64_t horizon, double& seconds, bool& limited) {
  const auto cutoff = std::max(start, now > horizon ? now - horizon : std::uint64_t{0});
  const auto since = std::max(cutoff, retained_since);
  while (!events.empty() && events.front() <= since) events.pop_front();
  seconds = static_cast<double>(now - since) / billion;
  limited = limited || retained_since > cutoff;
  return seconds > 0 ? static_cast<double>(events.size()) / seconds : 0.0;
}
void RuntimeDiagnostics::ready() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_ready_ || terminal_state_) throw std::logic_error("Diagnostic session already started or stopped");
  ready_ = now_locked(); is_ready_ = true; values_.state = "waiting_for_frame";
}
void RuntimeDiagnostics::read_started() {
  std::lock_guard<std::mutex> lock(mutex_); require_running(); now_locked();
  if (values_.read_in_progress) throw std::logic_error("Read already in progress");
  ++values_.read_attempts; values_.read_in_progress = true;
}
void RuntimeDiagnostics::frame_received() {
  std::lock_guard<std::mutex> lock(mutex_); require_running();
  const auto now = now_locked();
  if (!values_.read_in_progress) throw std::logic_error("No read in progress");
  ++values_.captured_frames; values_.consecutive_empty_reads = 0;
  values_.read_in_progress = false; has_frame_ = true; last_frame_ = now;
  captures_.add(now);
}
void RuntimeDiagnostics::empty_read() {
  std::lock_guard<std::mutex> lock(mutex_); require_running(); now_locked();
  if (!values_.read_in_progress) throw std::logic_error("No read in progress");
  ++values_.empty_reads; ++values_.consecutive_empty_reads; values_.read_in_progress = false;
}
void RuntimeDiagnostics::rejected_frame() {
  std::lock_guard<std::mutex> lock(mutex_); require_running(); now_locked();
  ++values_.frame_rejections; values_.read_in_progress = false;
}
void RuntimeDiagnostics::read_exception() {
  std::lock_guard<std::mutex> lock(mutex_); require_running(); now_locked();
  ++values_.read_exceptions; values_.read_in_progress = false;
}
void RuntimeDiagnostics::pair_published(bool composite) {
  std::lock_guard<std::mutex> lock(mutex_); require_running();
  const auto now = now_locked();
  if (values_.published_pairs >= values_.captured_frames)
    throw std::logic_error("Cannot publish a pair without a new accepted frame");
  ++values_.published_pairs;
  if (composite) ++values_.composite_publications;
  publications_.add(now);
}
void RuntimeDiagnostics::fail(const std::string& stage, const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (terminal_state_) return;
  terminal_ = now_locked(); terminal_state_ = true;
  values_.state = "error"; values_.level = 2;
  values_.error_stage = stage; values_.detail = message; values_.read_in_progress = false;
}
void RuntimeDiagnostics::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (terminal_state_) return;
  terminal_ = now_locked(); terminal_state_ = true;
  values_.state = "stopped"; values_.level = 0; values_.read_in_progress = false;
}
void RuntimeDiagnostics::reset_rate_reference(double fps) {
  if (!std::isfinite(fps) || fps <= 0 || fps > 10000)
    throw std::invalid_argument("Invalid runtime FPS reference");
  std::lock_guard<std::mutex> lock(mutex_); require_running();
  ready_ = now_locked(); config_.expected_fps = fps;
  captures_ = Window{}; publications_ = Window{};
}
DiagnosticSnapshot RuntimeDiagnostics::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto observed = now_locked();
  const auto now = terminal_state_ ? terminal_ : observed;
  auto out = values_;
  out.expected_fps_reference = config_.expected_fps;
  out.uptime_seconds = static_cast<double>(now - started_) / billion;
  if (has_frame_) out.last_frame_age_seconds = static_cast<double>(now - last_frame_) / billion;
  if (is_ready_) {
    out.capture_fps = captures_.rate(now, ready_, nanoseconds(config_.window_seconds),
        out.capture_window_seconds, out.window_limited);
    out.publication_fps = publications_.rate(now, ready_, nanoseconds(config_.window_seconds),
        out.publication_window_seconds, out.window_limited);
  }
  if (terminal_state_) return out;
  const auto anchor = has_frame_ ? last_frame_ : (is_ready_ ? ready_ : started_);
  if (now - anchor >= nanoseconds(config_.stale_after_seconds)) {
    out.state = "stale"; out.level = 3;
    out.detail = "No accepted frame within the freshness timeout; source may still be blocked";
  } else if (!is_ready_ || !has_frame_) {
    out.state = is_ready_ ? "waiting_for_frame" : "starting"; out.level = 1;
    out.detail = "Waiting for the first accepted runtime frame";
  } else if (out.consecutive_empty_reads > 0) {
    out.state = "degraded"; out.level = 1; out.detail = "Source returned an empty/failed read";
  } else if (config_.expected_fps > 0 && out.publication_window_seconds >= config_.window_seconds &&
             out.publication_fps < config_.expected_fps * config_.minimum_fps_ratio) {
    out.state = "degraded"; out.level = 1; out.detail = "Publication rate below monitoring reference";
  } else {
    out.state = "running"; out.level = 0; out.detail = "Accepted frames are being acquired";
  }
  return out;
}

MonitoredCapture::MonitoredCapture(std::unique_ptr<CaptureSource> source,
                                 RuntimeDiagnostics& diagnostics, unsigned maximum)
    : session_(std::move(source)), diagnostics_(diagnostics), maximum_(maximum) {
  if (maximum_ == 0) throw std::invalid_argument("Empty read limit must be positive");
}
void MonitoredCapture::open(const CaptureConfig& c) {
  try { session_.open(c); diagnostics_.ready(); }
  catch (const std::exception& e) { diagnostics_.fail("startup", e.what()); throw; }
}
bool MonitoredCapture::read(CapturedFrame& frame) {
  diagnostics_.read_started();
  bool success;
  try { success = session_.read(frame); }
  catch (const FrameValidationError& e) {
    diagnostics_.rejected_frame(); diagnostics_.fail("frame_validation", e.what()); throw;
  } catch (const std::exception& e) {
    diagnostics_.read_exception(); diagnostics_.fail("source_read", e.what()); throw;
  }
  if (!success) {
    diagnostics_.empty_read();
    if (++consecutive_ >= maximum_) {
      diagnostics_.fail("empty_read_limit", "Consecutive frame read failure limit reached");
      throw std::runtime_error("Consecutive frame read failure limit reached");
    }
    return false;
  }
  consecutive_ = 0; diagnostics_.frame_received(); return true;
}
void MonitoredCapture::close() { session_.close(); diagnostics_.stop(); }
std::string MonitoredCapture::description() const { return session_.description(); }
}  // namespace ardustereo

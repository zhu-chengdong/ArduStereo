#pragma once

#include "ardustereo/diagnostics.hpp"
#include <diagnostic_msgs/DiagnosticArray.h>
#include <ros/callback_queue.h>
#include <ros/ros.h>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ardustereo {
class RosDiagnostics {
 public:
  RosDiagnostics(ros::NodeHandle nh, RuntimeDiagnostics& monitor, double period,
                 std::string identity, DiagnosticConfig config)
      : nh_(nh), monitor_(monitor), identity_(std::move(identity)), config_(config),
        spinner_(1, &queue_) {
    nh_.setCallbackQueue(&queue_);
    publisher_ = nh_.advertise<diagnostic_msgs::DiagnosticArray>("/diagnostics", 1, true);
    timer_ = nh_.createWallTimer(ros::WallDuration(period),
        [this](const ros::WallTimerEvent&) { publish(); });
    publish();
    spinner_.start();
  }
  ~RosDiagnostics() { timer_.stop(); spinner_.stop(); }
  void finish() {
    timer_.stop(); spinner_.stop();
    const auto s = monitor_.snapshot();
    publish_snapshot(s);  // Best effort on ROS shutdown; summary is also logged.
    // roscpp shuts rosconsole down before ros::ok() becomes false. Keep the
    // final summary available even after SIGINT has disabled ROS logging.
    std::clog << "ArduStereo final state=" << s.state << " error_stage=" << s.error_stage
        << " read_attempts=" << s.read_attempts << " captured_frames=" << s.captured_frames
        << " published_pairs=" << s.published_pairs << " empty_reads=" << s.empty_reads
        << " frame_rejections=" << s.frame_rejections << " read_exceptions=" << s.read_exceptions
        << std::endl;
  }
 private:
  template <typename T> static void value(diagnostic_msgs::DiagnosticStatus& s,
                                        const std::string& key, const T& v) {
    diagnostic_msgs::KeyValue kv;
    kv.key = key;
    std::ostringstream stream; stream << std::setprecision(12) << std::boolalpha << v;
    kv.value = stream.str(); s.values.push_back(kv);
  }
  void publish() {
    try { publish_snapshot(monitor_.snapshot()); }
    catch (const std::exception& e) { ROS_ERROR_STREAM("Diagnostic callback failed: " << e.what()); }
  }
  void publish_snapshot(const DiagnosticSnapshot& s) {
    diagnostic_msgs::DiagnosticArray array;
    array.header.stamp = ros::Time::now();
    diagnostic_msgs::DiagnosticStatus status;
    status.name = ros::this_node::getName() + ": acquisition";
    status.hardware_id = identity_;
    status.level = static_cast<std::uint8_t>(s.level); status.message = s.detail.empty() ? s.state : s.detail;
#define FIELD(name) value(status, #name, s.name)
    FIELD(state); FIELD(error_stage); FIELD(uptime_seconds); FIELD(read_in_progress);
    FIELD(read_attempts); FIELD(captured_frames); FIELD(published_pairs); FIELD(composite_publications);
    FIELD(empty_reads); FIELD(consecutive_empty_reads); FIELD(frame_rejections); FIELD(read_exceptions);
    FIELD(capture_fps); FIELD(publication_fps); FIELD(capture_window_seconds);
    FIELD(publication_window_seconds); FIELD(last_frame_age_seconds); FIELD(window_limited);
#undef FIELD
    value(status, "device_drop_count", "unavailable");
    value(status, "subscriber_received_count", "unavailable");
    value(status, "rate_clock", "steady_monotonic");
    value(status, "image_timestamp", "host_receive_not_sensor_exposure");
    value(status, "expected_fps_reference", config_.expected_fps);
    value(status, "window_seconds_requested", config_.window_seconds);
    value(status, "frame_timeout_seconds", config_.stale_after_seconds);
    array.status.push_back(status); publisher_.publish(array);
  }
  ros::CallbackQueue queue_;
  ros::NodeHandle nh_;
  RuntimeDiagnostics& monitor_;
  std::string identity_;
  DiagnosticConfig config_;
  ros::Publisher publisher_;
  ros::WallTimer timer_;
  ros::AsyncSpinner spinner_;
};
}  // namespace ardustereo

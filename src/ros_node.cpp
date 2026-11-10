#include "ros_node.hpp"
#include "ros_diagnostics.hpp"
#include "ardustereo/opencv_capture.hpp"
#include "ardustereo/controls.hpp"
#include <ardustereo/GetControls.h>
#include <ardustereo/SetControls.h>

#include <camera_info_manager/camera_info_manager.h>
#include <ros/ros.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/Image.h>
#include <xmlrpcpp/XmlRpcValue.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
struct NodeConfig {
  ardustereo::CaptureConfig capture;
  std::string source = "v4l2";
  bool publish_composite = true;
  int queue_size = 1;
  int max_read_failures = 10;
  double synthetic_fps = 20.0;
  ardustereo::DiagnosticConfig diagnostics;
  double diagnostics_period = 1.0;
  std::string composite_frame_id = "camera";
  std::string left_frame_id = "camera_left_optical_frame";
  std::string right_frame_id = "camera_right_optical_frame";
  std::string left_camera_info_url;
  std::string right_camera_info_url;
  bool require_calibration = false;
};

// One schema for ROS parameters. Unknown keys and type errors are fatal.
template <typename T>
void param(ros::NodeHandle& nh, const std::string& key, T& value,
           std::set<std::string>& allowed) {
  allowed.insert(key);
  XmlRpc::XmlRpcValue raw;
  if (nh.getParam(key, raw)) {
    const auto type = raw.getType();
    const bool correct_type =
        (std::is_same<T, std::string>::value && type == XmlRpc::XmlRpcValue::TypeString) ||
        (std::is_same<T, bool>::value && type == XmlRpc::XmlRpcValue::TypeBoolean) ||
        (std::is_same<T, int>::value && type == XmlRpc::XmlRpcValue::TypeInt) ||
        (std::is_same<T, double>::value && (type == XmlRpc::XmlRpcValue::TypeDouble ||
                                           type == XmlRpc::XmlRpcValue::TypeInt));
    if (!correct_type || !nh.getParam(key, value))
      throw std::invalid_argument("Parameter has the wrong type: " + key);
  }
}

NodeConfig load_config(ros::NodeHandle& nh) {
  NodeConfig c;
  std::set<std::string> allowed;
#define PARAM(name) param(nh, #name, c.name, allowed)
#define CAPTURE_PARAM(name) param(nh, #name, c.capture.name, allowed)
  CAPTURE_PARAM(device); CAPTURE_PARAM(width); CAPTURE_PARAM(height);
  CAPTURE_PARAM(split_column); CAPTURE_PARAM(warmup_frames); CAPTURE_PARAM(buffer_size);
  CAPTURE_PARAM(input_format); CAPTURE_PARAM(mono16_shift); CAPTURE_PARAM(fourcc);
  CAPTURE_PARAM(frame_rate); CAPTURE_PARAM(exposure); CAPTURE_PARAM(analogue_gain);
  PARAM(source); PARAM(publish_composite); PARAM(queue_size); PARAM(max_read_failures);
  PARAM(synthetic_fps); PARAM(composite_frame_id); PARAM(left_frame_id); PARAM(right_frame_id);
  PARAM(left_camera_info_url); PARAM(right_camera_info_url); PARAM(require_calibration);
  PARAM(diagnostics_period);
  param(nh, "diagnostics_window", c.diagnostics.window_seconds, allowed);
  param(nh, "frame_timeout", c.diagnostics.stale_after_seconds, allowed);
  param(nh, "diagnostic_expected_fps", c.diagnostics.expected_fps, allowed);
  param(nh, "diagnostic_min_fps_ratio", c.diagnostics.minimum_fps_ratio, allowed);
#undef PARAM
#undef CAPTURE_PARAM
  XmlRpc::XmlRpcValue root;
  if (nh.getParam("", root)) {
    if (root.getType() != XmlRpc::XmlRpcValue::TypeStruct)
      throw std::invalid_argument("Private node parameters must be a mapping");
    for (auto it = root.begin(); it != root.end(); ++it)
      if (allowed.count(it->first) == 0)
        throw std::invalid_argument("Unsupported parameter: " + it->first);
  }
  ardustereo::validate_config(c.capture);
  ardustereo::validate_diagnostic_config(c.diagnostics);
  if (!std::isfinite(c.diagnostics_period) || c.diagnostics_period < 0.05 || c.diagnostics_period > 60)
    throw std::invalid_argument("Invalid diagnostics_period (allowed range 0.05..60 seconds)");
  if (c.source != "v4l2" && c.source != "synthetic")
    throw std::invalid_argument("source must be v4l2 or synthetic");
  if (c.queue_size < 1 || c.queue_size > 100 || c.max_read_failures < 1 ||
      c.max_read_failures > 10000 || !std::isfinite(c.synthetic_fps) ||
      c.synthetic_fps <= 0 || c.synthetic_fps > 500)
    throw std::invalid_argument("Invalid queue_size, max_read_failures or synthetic_fps");
  if (c.left_frame_id.empty() || c.right_frame_id.empty() || c.composite_frame_id.empty() ||
      c.left_frame_id == c.right_frame_id || c.left_frame_id.front() == '/' ||
      c.right_frame_id.front() == '/' || c.composite_frame_id.front() == '/')
    throw std::invalid_argument("Frame IDs must be nonempty, relative, and left/right must differ");
  bool sim_time = false;
  ros::param::param<bool>("/use_sim_time", sim_time, false);
  if (sim_time) throw std::invalid_argument("Live/synthetic acquisition uses host receive time; use_sim_time must be false");
  return c;
}

ardustereo::Calibration calibration_contract(const sensor_msgs::CameraInfo& info) {
  ardustereo::Calibration c;
  c.width = static_cast<int>(info.width);
  c.height = static_cast<int>(info.height);
  c.distortion_model = info.distortion_model;
  c.d = info.D;
  std::copy(info.K.begin(), info.K.end(), c.k.begin());
  std::copy(info.R.begin(), info.R.end(), c.r.begin());
  std::copy(info.P.begin(), info.P.end(), c.p.begin());
  if (info.binning_x > 1 || info.binning_y > 1 || info.roi.x_offset || info.roi.y_offset ||
      info.roi.width || info.roi.height || info.roi.do_rectify)
    throw std::invalid_argument("Cropped/binned CameraInfo is not supported in this version");
  return c;
}

class CameraInfoSource {
 public:
  CameraInfoSource(ros::NodeHandle nh, const std::string& name, const std::string& url,
                   int width, int height, bool required)
      : width_(width), height_(height), required_(required) {
    if (!url.empty()) {
      manager_.reset(new camera_info_manager::CameraInfoManager(nh, name));
      if (!manager_->loadCameraInfo(url))
        throw std::invalid_argument("Failed to load explicitly requested calibration for " + name);
    } else if (required) {
      throw std::invalid_argument("Calibration URL is required for " + name);
    } else {
      ROS_WARN_STREAM(name << " is uncalibrated (K[0]=0); no historical calibration is assumed");
    }
    get();
  }

  sensor_msgs::CameraInfo get() {
    sensor_msgs::CameraInfo info;
    if (manager_) info = manager_->getCameraInfo();
    else { info.width = static_cast<std::uint32_t>(width_); info.height = static_cast<std::uint32_t>(height_); }
    ardustereo::validate_calibration(calibration_contract(info), width_, height_, required_);
    return info;
  }
 private:
  int width_, height_;
  bool required_;
  std::unique_ptr<camera_info_manager::CameraInfoManager> manager_;
};

sensor_msgs::Image image_message(const ardustereo::MonoImage& image,
                                 const ros::Time& stamp, const std::string& frame_id) {
  ardustereo::validate_image(image);
  sensor_msgs::Image msg;
  msg.header.stamp = stamp;
  msg.header.frame_id = frame_id;
  msg.width = static_cast<std::uint32_t>(image.width);
  msg.height = static_cast<std::uint32_t>(image.height);
  msg.encoding = "mono8";
  msg.is_bigendian = 0;
  msg.step = static_cast<std::uint32_t>(image.width);
  msg.data = image.data;
  return msg;
}
}  // namespace

int ardustereo::run_ros_node(ros::NodeHandle& nh, ros::NodeHandle& private_nh,
                              std::unique_ptr<CaptureSource> supplied_source) {
  std::unique_ptr<RuntimeDiagnostics> monitor;
  std::unique_ptr<RosDiagnostics> diagnostics;
  std::unique_ptr<MonitoredCapture> session;
  try {
    auto c = load_config(private_nh);
    monitor.reset(new RuntimeDiagnostics(c.diagnostics));
    diagnostics.reset(new RosDiagnostics(nh, *monitor, c.diagnostics_period,
        c.source == "synthetic" ? "synthetic_no_camera" : c.capture.device, c.diagnostics));
    ros::NodeHandle left_nh(nh, "left");
    ros::NodeHandle right_nh(nh, "right");
    CameraInfoSource left_info(left_nh, "ardustereo_left", c.left_camera_info_url,
        c.capture.split_column, c.capture.height, c.require_calibration);
    CameraInfoSource right_info(right_nh, "ardustereo_right", c.right_camera_info_url,
        c.capture.width - c.capture.split_column, c.capture.height, c.require_calibration);
    auto composite_pub = nh.advertise<sensor_msgs::Image>("image_raw", c.queue_size);
    auto left_pub = left_nh.advertise<sensor_msgs::Image>("image_raw", c.queue_size);
    auto right_pub = right_nh.advertise<sensor_msgs::Image>("image_raw", c.queue_size);
    auto left_info_pub = left_nh.advertise<sensor_msgs::CameraInfo>("camera_info", c.queue_size);
    auto right_info_pub = right_nh.advertise<sensor_msgs::CameraInfo>("camera_info", c.queue_size);
    if (c.source == "synthetic") ROS_WARN("SYNTHETIC INPUT: this is not a camera/hardware test");
    if (!supplied_source) supplied_source = c.source == "synthetic"
        ? ardustereo::make_synthetic_source() : ardustereo::make_opencv_v4l2_source();
    session.reset(new MonitoredCapture(std::move(supplied_source), *monitor,
        static_cast<unsigned>(c.max_read_failures)));
    session->open(c.capture);
    ROS_INFO_STREAM(session->description());
    ROS_INFO("Timestamp: host system time immediately after source read, not sensor exposure time");
    std::unique_ptr<ros::WallRate> synthetic_rate(new ros::WallRate(c.synthetic_fps));
    auto simulated_controls = c.source == "synthetic" ? make_simulated_controls() : nullptr;
    if (simulated_controls) {
      auto initial = read_controls(*simulated_controls).values;
      initial[0] = static_cast<int>(c.synthetic_fps);
      change_controls(*simulated_controls, initial);
    }
    const auto fill_controls = [&](auto& response, const ControlState& state) {
      response.success = true; response.simulated = c.source == "synthetic";
      response.frame_rate = state.values[0]; response.exposure = state.values[1];
      response.analogue_gain = state.values[2];
      for (unsigned i = 0; i < 3; ++i) {
        response.minimum[i] = state.ranges[i].minimum;
        response.maximum[i] = state.ranges[i].maximum; response.step[i] = state.ranges[i].step;
      }
      response.message = response.simulated ? "SIMULATED controls; exposure/gain have no physical effect" : "Driver controls read back";
    };
    auto get_controls = nh.advertiseService<GetControls::Request, GetControls::Response>("get_controls",
        [&](GetControls::Request&, GetControls::Response& response) {
          response.simulated = c.source == "synthetic";
          try {
            auto device = simulated_controls ? nullptr : make_linux_controls(c.capture.device);
            fill_controls(response, read_controls(simulated_controls ? *simulated_controls : *device));
          } catch (const std::exception& error) { response.success = false; response.message = error.what(); }
          return true;
        });
    auto set_controls = nh.advertiseService<SetControls::Request, SetControls::Response>("set_controls",
        [&](SetControls::Request& request, SetControls::Response& response) {
          response.simulated = c.source == "synthetic";
          try {
            if (request.frame_rate <= 0) throw std::invalid_argument("frame_rate must be positive");
            auto device = simulated_controls ? nullptr : make_linux_controls(c.capture.device);
            auto& transport = simulated_controls ? *simulated_controls : *device;
            const auto before = read_controls(transport);
            const auto after = change_controls(transport, {{request.frame_rate, request.exposure, request.analogue_gain}});
            if (before.values[0] != after.values[0]) {
              monitor->reset_rate_reference(after.values[0]);
              if (simulated_controls) synthetic_rate.reset(new ros::WallRate(after.values[0]));
            }
            for (const auto& item : std::array<std::pair<const char*, int>, 3>{{
                {"frame_rate", after.values[0]}, {"exposure", after.values[1]}, {"analogue_gain", after.values[2]}}})
              if (!simulated_controls) private_nh.setParam(item.first, item.second);
            fill_controls(response, after);
            ROS_INFO_STREAM("Runtime controls verified; simulated=" << static_cast<bool>(response.simulated) << " frame_rate=" << after.values[0]
                            << " exposure=" << after.values[1] << " analogue_gain=" << after.values[2]);
          } catch (const std::exception& error) { response.success = false; response.message = error.what(); }
          return true;
        });
    while (ros::ok()) {
      ardustereo::CapturedFrame frame;
      if (!session->read(frame)) {
        ROS_WARN_THROTTLE(2.0, "Frame read failed; no stale image is published");
        ros::spinOnce();
        ros::WallDuration(0.01).sleep();
        continue;
      }
      auto pair = ardustereo::split_stereo(frame.image, c.capture.split_column);
      ros::Time stamp;
      stamp.fromNSec(frame.received_system_ns);
      auto l_info = left_info.get();
      auto r_info = right_info.get();
      auto l_msg = image_message(pair.left, stamp, c.left_frame_id);
      auto r_msg = image_message(pair.right, stamp, c.right_frame_id);
      l_info.header = l_msg.header;
      r_info.header = r_msg.header;
      left_pub.publish(l_msg);
      right_pub.publish(r_msg);
      left_info_pub.publish(l_info);
      right_info_pub.publish(r_info);
      if (c.publish_composite)
        composite_pub.publish(image_message(frame.image, stamp, c.composite_frame_id));
      monitor->pair_published(c.publish_composite);
      ros::spinOnce();
      if (c.source == "synthetic") synthetic_rate->sleep();
    }
    session->close();
    diagnostics->finish();
    return 0;
  } catch (const std::exception& error) {
    if (monitor) monitor->fail("node", error.what());
    if (session) session->close();
    if (diagnostics) diagnostics->finish();
    ROS_FATAL_STREAM("ArduStereo stopped: " << error.what());
    return 1;
  }
}

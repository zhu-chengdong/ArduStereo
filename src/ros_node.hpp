#pragma once
#include "ardustereo/diagnostics.hpp"
#include <ros/ros.h>

namespace ardustereo {
// Optional source injection is used only by a separately built test executable.
int run_ros_node(ros::NodeHandle& nh, ros::NodeHandle& private_nh,
                 std::unique_ptr<CaptureSource> supplied_source = nullptr);
}

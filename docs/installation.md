# Installation and deployment

## Prerequisites

The tested camera environment is Ubuntu20.04 / L4T35.5.0 / kernel5.10.192-tegra on Jetson Orin Nano8GB, with ROS Noetic and OpenCV4.5.4. The existing Arducam driver must already expose the intended camera as a usable V4L2 device. Match the actual driver, device tree, kernel and camera; no other Jetson combination is implied by this test.

Build requirements are CMake>=3.16, a C++14 compiler, Threads, catkin, roscpp, sensor_msgs, camera_info_manager, diagnostic_msgs and OpenCV4 core/videoio development files. The companion needs Python3, rospy and std_srvs. ROS tests additionally need rostest; static YAML/package checks use PyYAML. They are declared in package.xml or documented here, not installed by this source package.

Inspect existing dependencies before changing a research platform. Useful read-only checks include `rosversion -d`, `pkg-config --modversion opencv4`, and `v4l2-ctl -d /dev/video0 --list-formats-ext --list-ctrls` when those tools are installed. A missing device or failed camera probe must be resolved separately from this application.

## Build

Follow the source checkout and `catkin_make` commands in the [README](../README.md). RelWithDebInfo matches the tested application's `-O2 -g -DNDEBUG` configuration. Source `devel/setup.bash` in each terminal; no `.bashrc` edit is required.

For a core-only build, use [local_build.md](local_build.md). The validated camera path was tested from a devel workspace. Catkin install rules are provided, but release-specific real-camera install-space validation is not claimed.

## First use

Review config/b0266.yaml. Confirm device, GREY geometry and the observed driver-specific raw controls. Start `roslaunch ardustereo stereo.launch`, inspect the readback/startup log and diagnostics, and stop with Ctrl-C. No additional roscore or control-setting shell is needed for a fresh normal launch.

Hardware control values are validated against the observed names/ranges/steps and read back. Unsupported mappings, failed writes or mismatches fail startup. An attempted rollback follows partial setting failures; this is not a guarantee that every possible driver can restore state. Gain/exposure unit conversion is not inferred.

## Calibration and clocks

Supply standard ROS calibration YAML through left_camera_info_url/right_camera_info_url if available. The node validates the message contract, not the physical calibration. The default empty URLs give explicit uncalibrated CameraInfo.

Image stamps are host wall-clock receive times; rates and durations use a monotonic clock. The records flag implausibly old wall-clock dates. Multi-sensor experiments still need a separately verified clock/synchronization arrangement.

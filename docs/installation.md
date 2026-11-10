# Installation and prerequisites

This release requires an already working camera driver and an existing ROS Noetic development environment. It does not install drivers, firmware, device trees, JetPack, a kernel or ROS. A missing dependency should be resolved deliberately for the user's platform, not by running an opaque driver script.

The verified rig is documented in [compatibility](compatibility.md). Required build/runtime components are a C++14 compiler, CMake >=3.16, catkin, OpenCV4 with core/videoio, roscpp, sensor_msgs, camera_info_manager, diagnostic_msgs, message_generation/message_runtime, rospy, std_srvs and Python3 PyYAML. Tests additionally use rostest. The GUI uses Python's standard HTTP server and plain local HTML/JS; it needs no Qt, Node or external web framework.

Read-only dependency checks in an existing sourced Noetic shell:

```bash
command -v cmake
command -v catkin_make
pkg-config --modversion opencv4
python3 -c 'import yaml, rospy'
rospack find message_generation
rospack find message_runtime
rospack find camera_info_manager
rospack find diagnostic_msgs
rospack find std_srvs
```

Use the independent-workspace build commands in the [README](../README.md#build-and-use). `catkin_make` writes only that workspace. Source its `devel/setup.bash`; system-wide `make install` is unnecessary. Build first, then launch the viewer; starting the viewer alone does not open the camera.

Before physical use, confirm power, video node, supported format and raw control interfaces on the actual rig. No universal Jetson compatibility is assumed. Stop other camera consumers before starting ArduStereo. For a hardware-free check, use [synthetic tests](local_build.md).

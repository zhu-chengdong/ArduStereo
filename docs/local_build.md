# Reproducible hardware-free checks

The core has no ROS or OpenCV dependency. With an existing C++14/CMake toolchain:

```bash
cmake -S . -B build/core -DARDUSTEREO_BUILD_ROS=OFF -DARDUSTEREO_BUILD_OPENCV=OFF
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure
python3 test/test_session_store.py
python3 validation/verify_timing.py
```

For the full Noetic package in an existing catkin workspace:

```bash
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPYTHON_EXECUTABLE=/usr/bin/python3
source devel/setup.bash
catkin_make run_tests_ardustereo
catkin_test_results build/test_results
```

Static checks are `python3 tools/check_package.py`. A camera-free ROS demonstration is `roslaunch ardustereo stereo.launch config:=$(rospack find ardustereo)/config/synthetic.yaml`. That profile requests no hardware controls and is explicitly marked synthetic. Do not substitute it for actual camera validation.

Existing author-run tests passed in an isolated x86_64 Ubuntu20.04/Noetic environment using OpenCV4.2.0 and on the target aarch64 environment using OpenCV4.5.4. Counts and scope are in [validation](validation.md). CI uses the pinned image in the workflow file. A future build/CI result is separate evidence from those archived runs.

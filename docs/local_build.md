# Hardware-free build and checks

These commands are for a separate local workspace with existing dependencies. They do not access a camera. Simulation and model results must remain separate from real camera evidence.

Core-only C++ checks:

```bash
cmake -S . -B build/core -DARDUSTEREO_BUILD_ROS=OFF -DARDUSTEREO_BUILD_OPENCV=OFF
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure
```

Python/static/numeric checks, with existing PyYAML:

```bash
python3 tools/check_package.py
python3 test/test_session_store.py
python3 test/test_viewer.py
python3 validation/gui-fix1/verify_results.py
```

POSIX-specific process-lifecycle cases skip on Windows. Do not add duplicate platform runs as distinct test cases. Linux startup-control tests wrap V4L2 calls, not real devices.

In an existing Noetic catkin workspace:

```bash
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPYTHON_EXECUTABLE=/usr/bin/python3
source devel/setup.bash
catkin_make run_tests_ardustereo
catkin_test_results build/test_results
```

ROS tests use synthetic inputs and injected faults. `roslaunch --dump-params` or `--nodes` can inspect a launch without starting capture. Actual camera behavior is reported separately in [hardware validation](../validation/README.md).

The source GUI candidate previously passed local x86_64 model/synthetic tests and target ARM build/tests. The release CI reruns hardware-free checks for this public commit; its result must be checked separately and is not proof of camera compatibility. Historical 0.2.0 and0.2.2 build details remain at the historical release rather than being presented as this candidate's new results.

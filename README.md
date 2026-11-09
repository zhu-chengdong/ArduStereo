# ArduStereo

ArduStereo is a lightweight ROS 1 acquisition package for an Arducam B0266 / Camarray HAT stereo rig. It reuses OpenCV's V4L2 backend and the installed camera driver, loads camera settings from YAML, and publishes standard left/right images and CameraInfo. An optional companion node records experiment metadata and saves paired snapshots on demand.

**Release 0.2.2:** the tested acquisition implementation is frozen. This public distribution adds current documentation and reproducible, de-identified timing evidence. It is validated on the platform below; it is not a general compatibility claim for other Jetson boards, kernels or cameras.

## Validated configuration

| Component | Tested value |
|---|---|
| Host | Jetson Orin Nano 8 GB, Ubuntu 20.04, L4T 35.5.0, kernel 5.10.192-tegra |
| Middleware/backend | ROS Noetic, OpenCV 4.5.4, CAP_V4L2 |
| Camera | Arducam B0266, dual OV9281, Camarray HAT |
| Input | `/dev/video0`, GREY, 2560 x 800, horizontally packed |
| Outputs | Left/right 1280 x 800, mono8; two CameraInfo; optional composite |
| Startup raw controls | frame_rate=20, exposure=681, analogue_gain=100 |

The raw controls are specific to the observed driver. Exposure and gain are not converted to physical units. A working matching vendor driver and video device are prerequisites; this repository does not install or redistribute kernel drivers, firmware or device trees.

## Build and start

Use an existing ROS Noetic environment with the dependencies in [installation](docs/installation.md). These commands do not upgrade JetPack, the kernel or ROS.

```bash
mkdir -p ~/ardustereo_ws/src
cd ~/ardustereo_ws/src
git clone --branch v0.2.2 https://github.com/zhu-chengdong/ArduStereo.git ardustereo
cd ~/ardustereo_ws
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPYTHON_EXECUTABLE=/usr/bin/python3
source devel/setup.bash
roslaunch ardustereo stereo.launch
```

Edit `config/b0266.yaml` before restarting to choose settings. A fresh `roslaunch` starts its ROS master automatically if needed. The three requested controls are applied and read back by the backend; no separate `v4l2-ctl -c` commands are needed for normal use.

## Topics and records

| Interface | Default name |
|---|---|
| Composite image | `/camera/image_raw` |
| Stereo images | `/camera/left/image_raw`, `/camera/right/image_raw` |
| Calibration messages | `/camera/left/camera_info`, `/camera/right/camera_info` |
| Diagnostics | `/diagnostics` |
| Paired snapshot service | `/camera/save_snapshot` (`std_srvs/Trigger`) |

Without supplied calibration URLs the CameraInfo messages are explicitly uncalibrated (`K[0]=0`). Common image stamps identify two halves of the same captured frame; they are host receive times and do not establish sensor exposure synchronization.

Automatic metadata records go to `~/ardustereo_sessions/session_<time>_<unique-id>/`. This is configuration/diagnostic recording, not continuous image recording. While acquisition is running, use a second sourced terminal for a snapshot:

```bash
rosservice call /camera/save_snapshot "{}"
```

Each successful request saves a fresh left/right PGM pair, CameraInfo JSON and a checksum manifest. The companion subscribes to images only during the request. See [record format](docs/experiment_records.md).

## Validation and observed rate variation

Hardware-free tests passed on x86_64 and the target ARM platform before real capture. The implementation also completed 60 minutes of real acquisition: 71,822 complete five-message groups, 19.9506 FPS at the independent composite subscriber, no measurement-period read faults or incomplete groups, and natural SIGINT exit in about 1.57 s.

The long run contained 20 low-rate diagnostic samples, with a minimum 5 s window of 16.8 FPS. The original strict no-WARN criterion therefore failed. An offline audit reproduced all 20 warnings from saved timings and source counters. The same-format legacy baseline also showed short output-rate dips. Nominal 20 FPS is not a guarantee of perfectly uniform 50 ms intervals or sensor-level zero loss. See [validation results](validation/README.md), including the preserved qualification and baseline differences.

## Hardware-free checks

```bash
cmake -S . -B build/core -DARDUSTEREO_BUILD_ROS=OFF -DARDUSTEREO_BUILD_OPENCV=OFF
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure
python3 test/test_session_store.py
python3 validation/verify_timing.py
```

The synthetic ROS profile is available for interface tests. It explicitly rejects hardware controls and never impersonates a camera. See [local tests](docs/local_build.md).

## Documentation

- [Installation](docs/installation.md) and [compatibility](docs/compatibility.md)
- [Architecture](docs/architecture.md), [interfaces](docs/interfaces.md), [diagnostics](docs/diagnostics.md)
- [Records and snapshots](docs/experiment_records.md), [validation](docs/validation.md)

Developed from the `mono_cam_cpp` workflow used for the author's York-Tank collection. The measurements supplied here are separate bench validations of ArduStereo. No SLAM accuracy or new underwater-dataset result is claimed by this release.

MIT licensed. See [provenance](THIRD_PARTY_NOTICES.md). Maintainer: Chengdong Zhu, University of York. Software citation metadata is in [CITATION.cff](CITATION.cff).

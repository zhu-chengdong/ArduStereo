# ArduStereo

A lightweight visual tool for **previewing, adjusting and acquiring images with an Arducam B0266 stereo camera** in a ROS research workflow.

ArduStereo brings left/right preview, frame-rate/exposure/gain controls, readback, actual rate display, start/stop, paired snapshots and YAML saving into one local browser console. It uses the installed camera driver and OpenCV/V4L2; it does not replace the kernel driver or supply depth estimation or SLAM.

**Public tested candidate:** [v0.3.0-startup-init-fix1](https://github.com/zhu-chengdong/ArduStereo/releases/tag/v0.3.0-startup-init-fix1). Package version is 0.3.0. The tested implementation is unchanged in this distribution; release documentation and de-identified numeric evidence are updated. The older [v0.2.2 release](https://github.com/zhu-chengdong/ArduStereo/releases/tag/v0.2.2) remains available with its original code and validation.

[中文快速说明](README.zh-CN.md) · [Installation](docs/installation.md) · [Viewer](docs/viewer.md) · [Validation](validation/README.md)

## Tested rig

| Component | Verified configuration |
|---|---|
| Computer | Jetson Orin Nano 8GB |
| System | Ubuntu 20.04, L4T 35.5.0, kernel 5.10.192-tegra |
| Middleware/backend | ROS Noetic 1.17.4, OpenCV 4.5.4, V4L2 |
| Camera | Arducam B0266, dual OV9281, Camarray HAT |
| Input | `/dev/video0`, GREY 2560×800, horizontally packed |
| Stereo output | Two 1280×800 mono8 images and two CameraInfo messages |
| Default raw controls | `frame_rate=20`, `exposure=681`, `analogue_gain=100` |

This is one verified hardware/system combination. A working matching vendor driver and video node are prerequisites. Exposure/gain are driver integers, not unverified physical-unit conversions. See [compatibility](docs/compatibility.md).

## Build and use

In an existing Noetic environment with the [listed dependencies](docs/installation.md):

```bash
mkdir -p ~/ardustereo_gui_ws/src
cd ~/ardustereo_gui_ws/src
git clone --branch v0.3.0-startup-init-fix1 https://github.com/zhu-chengdong/ArduStereo.git ardustereo
cd ~/ardustereo_gui_ws
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPYTHON_EXECUTABLE=/usr/bin/python3
source devel/setup.bash
roslaunch ardustereo viewer.launch
```

Open `http://127.0.0.1:8765/`, then click **启动采集** (Start). Launching the console alone does not start the camera. On a remote host, use your existing SSH access to forward this loopback port, as described in the viewer guide.

Inspect both images, change one control and click **应用参数** (Apply), confirm readback, and observe the actual rate after a complete 5-second window. **成对快照** saves a raw stereo pair with metadata. **保存YAML** saves current inputs to a new file; it does not by itself apply them. **停止采集** releases the owned capture process and permits a fresh start. Exiting the viewer terminal also cleans up its owned process. Closing the browser alone leaves acquisition running.

For headless acquisition with the same YAML defaults:

```bash
roslaunch ardustereo stereo.launch
```

Normal acquisition needs no separate `roscore` or three `v4l2-ctl -c` setters. The launcher starts a master if required. Existing external ROS processes are not taken over by the GUI.

## Research data

Published interfaces include `/camera/left/image_raw`, `/camera/right/image_raw`, both `camera_info` topics, optional `/camera/image_raw`, and `/diagnostics`. Images share a host receive stamp from the packed input; this is not a sensor exposure-time or physical-sync measurement. Without supplied calibration, CameraInfo is uncalibrated (`K[0]=0`).

Each acquisition writes configuration/diagnostic records under `~/ardustereo_sessions/`. Viewer actions, new YAML files and capture logs are under `~/ardustereo_gui/`. A paired snapshot saves fresh left/right PGM images, CameraInfo JSON and checksums. Automatic session records do **not** continuously write every image. Continuous recording uses external ROS tools, for example:

```bash
rosbag record --duration=10 -O stereo.bag /camera/left/image_raw /camera/right/image_raw /camera/left/camera_info /camera/right/camera_info /diagnostics
```

The GUI has no continuous-record button. Check storage before recording; raw image bags can be large.

## Actual validation, including rate warnings

The same tested candidate completed ARM build, GUI control/restore checks, eight paired snapshots and two saved YAML files on the rig above. Exposure and gain restoration changed the fixed-region mean by less than 0.36% on either side in that fixed scene.

With preview active, a fixed 3600-second window delivered **71,188 complete five-topic groups at 19.774 FPS**. It contained **133 low-publication-rate WARN samples**, a minimum **11.4 FPS** in fixed non-overlapping 5-second receive bins, and a maximum composite receive gap of **602.9 ms**. Window read/format fault counters were zero. WARN samples are not independent incident or sensor-drop counts. No constant-20-Hz or zero-sensor-loss claim is made.

Capture CPU averaged 18.97% of one logical core with 50.98MiB RSS. Viewer CPU averaged 23.73%; its RSS grew from first/last-five-minute means of 66.25 to 71.93MiB. This single hour does not demonstrate leak-free operation. Two natural GUI stops took 1.231/1.244s and restart was checked. New/legacy GREY workflows each received 2400 composite images in 120s at the same controls; this does not establish a long-term reliability advantage. External short rosbag recording/replay retained all 196 stereo groups with equal serialized payloads at 0.5× playback.

See [full qualifications and reproducible numeric checks](validation/README.md). Original images, bags, network logs and private journals are not uploaded. The historical v0.2.2 strict no-WARN failure remains unchanged at its tag.

## Hardware-free checks

```bash
cmake -S . -B build/core -DARDUSTEREO_BUILD_ROS=OFF -DARDUSTEREO_BUILD_OPENCV=OFF
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure
python3 test/test_session_store.py
python3 test/test_viewer.py
python3 validation/gui-fix1/verify_results.py
```

ROS synthetic mode is available through `viewer_synthetic.yaml`; it never impersonates a physical camera. See [local test commands](docs/local_build.md).

The frozen candidate UI still has a development banner saying hardware validation is pending, and may retain the last timestamp text after stop. These known display issues are preserved with the tested code; the validation status above comes from independent logs. See [release notes](RELEASE_NOTES.md).

MIT licensed. Developed by Chengdong Zhu, University of York, from the `mono_cam_cpp` workflow used for earlier York-Tank collection. That historical collection used the earlier node; these are separate bench validations. See [provenance](THIRD_PARTY_NOTICES.md) and [software citation](CITATION.cff).

# Compatibility and evidence boundaries

Verified on 2026-11-10 with `0.3.0-startup-init-fix1`:

| Component | Tested value |
|---|---|
| Camera | B0266, dual OV9281, Camarray HAT, MIPI CSI |
| Host | Jetson Orin Nano 8GB |
| System | Ubuntu20.04, L4T35.5.0, kernel5.10.192-tegra |
| Software | ROS Noetic1.17.4, OpenCV4.5.4 |
| Input | `/dev/video0`, GREY2560×800 |
| Controls | Raw frame_rate20/exposure681/analogue_gain100 |

ARM build, GUI preview and runtime controls, snapshot/YAML, an hour with preview, natural stops/restart and short external rosbag replay were checked on this rig. All rate warnings and the observed viewer RSS increase are reported in [validation](../validation/README.md).

This is not a support matrix for other Jetson models, carrier boards, L4T versions or camera modules. The installed vendor driver and device tree must match the user's actual system. This repository does not redistribute or upgrade them. No driver register mapping or exposure/gain physical-unit calibration is claimed.

Images have host receive timestamps. Equal message stamps denote software pairing from the packed frame, not a measured sensor synchronization bound. With default empty calibration URLs, CameraInfo is uncalibrated. Physical stereo calibration, underwater image quality, SLAM accuracy, independent-user replication and cold-power-cycle reliability are not established by these bench tests.

The old development banner and last timestamp text after stop are known display limitations in the frozen candidate. They were not edited merely for publication. No error or rate sample was removed to make the release appear more stable.

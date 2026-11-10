# ArduStereo v0.3.0-startup-init-fix1

Public tested candidate, package version0.3.0. The B0266 user workflow now includes left/right preview, online raw frame-rate/exposure/gain controls and readback, actual rate display, start/stop/restart, paired snapshots and new YAML saving. Continuous recording remains an external rosbag step.

This release publishes the exact previously tested implementation including its startup-control initialization fix. Executable source, build files, tests, launch files and YAML are byte-identical to that candidate; documentation, CI and de-identified validation materials are updated for publication. Original `mono_cam_cpp`, earlier candidates and v0.2.2 history remain preserved.

## Actual rig evidence

- Jetson Orin Nano8GB, Ubuntu20.04/L4T35.5.0/kernel5.10.192-tegra, Noetic1.17.4/OpenCV4.5.4, B0266/Camarray HAT.
- GREY2560×800 and raw20/681/100; real GUI control/restore checks,8 snapshot pairs and2 saved YAML files.
- Preview-active3600s:71,188 complete five-topic groups,19.774444FPS,133 low-rate WARN samples, minimum fixed5s11.4FPS, maximum composite receive gap602.9ms. Zero read/format faults refers to that window only; shutdown-boundary empty reads remain in the original logs.
- Capture mean CPU18.97% of one core/RSS50.98MiB; viewer mean CPU23.73%/RSS69.38MiB. Viewer first/last5min RSS66.25→71.93MiB; no leak-free claim.
- Two natural GUI stops1.231/1.244s and restart checked. New/legacy GREY flows each120s/2400images at identical controls, not proof of better long-term reliability.
- External short record/replay:196 complete stereo groups and equal serialized payloads at0.5× playback after camera stop.

## Qualifications

Rate warnings are retained, not equated with sensor-drop counts and not assigned an unverified hardware cause. This is one platform and one hour. No all-Jetson support, constant20Hz, physical calibration/sync bound or SLAM accuracy claim is made. The separate short-test network interruption, failed first legacy readiness attempt and corrected offline warning classification are disclosed in validation. They were not hidden by replacing raw logs.

Known frozen UI display issues are the old hardware-validation-pending banner and the last timestamp text after stop. They do not justify claiming untested capabilities. Original camera images, raw bags, device/network logs and unreviewed manuscript text are not included in the public package. Public evidence is numeric and de-identified.

See [validation](validation/README.md), [MIT license](LICENSE) and [citation](CITATION.cff). The previous [v0.2.2 release](https://github.com/zhu-chengdong/ArduStereo/releases/tag/v0.2.2) is unchanged, including its strict no-WARN failure.
